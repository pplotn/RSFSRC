#include "acd_defn.hh"
#include "grid.h"
#include "gridpp.hh"
#include "gridops.hh"
#include "istate.hh"
#include "iwop.hh"
#include "functions.hh"
#include "op.hh"
#include "blockop.hh"
#include "cgnealg.hh"
#include "LBFGSBT.hh"
#include "LinFitLS.hh"
#include "LinFitLSSM.hh"
#include "acdds_grad_selfdoc.hh"
#include <par.h>
#include "gradtest.hh"
#include "scantest.hh"
#include "segyops.hh"
#include "helmfftw.hh"

//#define GTEST_VERBOSE
IOKEY IWaveInfo::iwave_iokeys[]
= {
    {"csq",    0, true,  true },
    {"data",   1, false, true },
    {"source", 1, true,  false},
    {"",       0, false, false}
};

using RVL::parse;
using RVL::RVLException;
using RVL::Vector;
using RVL::Components;
using RVL::Operator;
using RVL::OperatorEvaluation;
using RVL::LinearOp;
using RVL::LinearOpFO;
using RVL::OpComp;
using RVL::SymmetricBilinearOp;
using RVL::AssignFilename;
using RVL::AssignParams;
using RVL::RVLRandomize;
using RVL::ScaleOpFwd;
using RVL::TensorOp;
using RVL::GradientTest;
using RVL::Scan;
using RVLUmin::CGNEPolicy;
using RVLUmin::CGNEPolicyData;
using RVLUmin::LBFGSBT;
using RVLUmin::LinFitLS;
using RVLUmin::LinFitLSSM;
using RVLUmin::CGNEAlg;
using TSOpt::IWaveEnvironment;
using TSOpt::IWaveTree;
using TSOpt::IWaveSampler;
using TSOpt::IWaveSim;
using TSOpt::TASK_RELN;
using TSOpt::IOTask;
using TSOpt::IWaveOp;
using TSOpt::SEGYTaperMute;
using TSOpt::GridMaskOp;
#ifdef IWAVE_USE_MPI
using TSOpt::MPIGridSpace;
using TSOpt::MPISEGYSpace;
#else
using TSOpt::GridSpace;
using TSOpt::SEGYSpace;
#endif
using TSOpt::GridExtendOp;
using TSOpt::GridDerivOp;
using TSOpt::GridHelmFFTWOp;

int xargc;
char **xargv;

/** this version requires that two model files be present:
 csqext - extended version of csq, with [d=spatial dimn]
 gdim=d+1
 dim=d
 n[d+1]=number of shot records,
 d[d+1]=1
 o[d+1]=0
 id[d+1]=d+1
 csq - physical space version of csq, with dim=gdim=d
 Uses these keywords for geometry only - data content irrelevant
 
 [intended near-term update: require only physical space material
 params, generate extended version internally, so automatic
 compatibility with data]
 
 */
int main(int argc, char ** argv) {
    
    try {
        
#ifdef IWAVE_USE_MPI
        int ts=0;
        MPI_Init_thread(&argc,&argv,MPI_THREAD_FUNNELED,&ts);
#endif
        
        PARARRAY * pars = NULL;
        FILE * stream = NULL;
        IWaveEnvironment(argc, argv, 0, &pars, &stream);
        
        if (retrieveGlobalRank()==0 && argc<2) {
            pagedoc();
            exit(0);
        }
        
#ifdef IWAVE_USE_MPI
        if (retrieveGroupID() == MPI_UNDEFINED) {
            fprintf(stream,"NOTE: finalize MPI, cleanup, exit\n");
            fflush(stream);
        }
        else {
#endif
            
            // the Op - note that it comes equpped with domain, range spaces!
            IWaveOp iwop(*pars,stream);
            
      SEGYTaperMute tnm(valparse<float>(*pars,"mute_slope",0.0f),
                        valparse<float>(*pars,"mute_zotime",0.0f),
                        valparse<float>(*pars,"mute_width",0.0f),0,
                        valparse<float>(*pars,"min",0.0f),
                        valparse<float>(*pars,"max",numeric_limits<float>::max()),
                        valparse<float>(*pars,"taper_width",0.0f),
                        valparse<int>(*pars,"taper_type",0),
                        valparse<float>(*pars,"time_width",0.0f),
                        valparse<float>(*pars,"sx_min",0.0f),
                        valparse<float>(*pars,"sx_max",numeric_limits<float>::max()),
                        valparse<float>(*pars,"sx_width",0.0f));

            LinearOpFO<float> tnmop(iwop.getRange(),iwop.getRange(),tnm,tnm);
            OpComp<float> op(iwop,tnmop);
            
            /* generate physical model space - a priori distinct from extended space
             without even the same grid */
#ifdef IWAVE_USE_MPI
            MPIGridSpace csqsp(valparse<std::string>(*pars,"csq"),"notype",true);
#else
            GridSpace csqsp(valparse<std::string>(*pars,"csq"),"notype",true);
#endif
            // make it a product, so it's compatible with domain of op
            StdProductSpace<ireal> dom(csqsp);
            // build grid extension op - checks compatibility between physical,
            // extended model-space specs
            
            
            // reference vel-squared model - assumed to be data used to spec
            // physical model space
            Vector<ireal> m(dom);
            AssignFilename mfn(valparse<std::string>(*pars,"csq"));
            Components<ireal> cm(m);
            cm[0].eval(mfn);
            
            // muted data - optionally archived
            Vector<ireal> mdd(op.getRange());
            std::string mddnm = valparse<std::string>(*pars,"datamut","");
            if (mddnm.size()>0) {
                AssignFilename mddfn(mddnm);
                mdd.eval(mddfn);
            }
            {
                // read data - only needed to define muted data
                Vector<ireal> dd(op.getRange());
                AssignFilename ddfn(valparse<std::string>(*pars,"data"));
                Components<ireal> cdd(dd);
                cdd[0].eval(ddfn);
                tnmop.applyOp(dd,mdd);
            }
            
            /* output stream */
            std::stringstream res,strgrad;
            res<<scientific;
            //strgrad<<scientific;
            
            // choice of GridDerivOp for semblance op is placeholder - extended axis is dim-th, with id=dim+1
            // note default weight of zero!!!
            // Note added 10.03.14: getGrid is not usably implemented for MPIGridSpace at this time, so
            // must fish the derivative index out by hand and bcast
            // THIS SHOULD ALL BE FIXED! (1) getGrid properly implemented in parallel, (2) proper
            // defn of DSOp to include internal extd axis case (space/time shift)
            int dsdir = INT_MAX;
            if (retrieveGlobalRank()==0) dsdir=csqsp.getGrid().dim;
#ifdef IWAVE_USE_MPI
            if (MPI_Bcast(&dsdir,1,MPI_INT,0,retrieveGlobalComm())) {
                RVLException e;
                e<<"Error: acdiva, rank="<<retrieveGlobalRank()<<"\n";
                e<<"  failed to bcast dsdir\n";
                throw e;
            }
#endif

      RPNT swind,ewind, width;
      RASN(swind,RPNT_0);
      RASN(ewind,RPNT_0);
      swind[0]=valparse<float>(*pars,"sww1",0.0f);
      swind[1]=valparse<float>(*pars,"sww2",0.0f);
      swind[2]=valparse<float>(*pars,"sww3",0.0f);
      ewind[0]=valparse<float>(*pars,"eww1",0.0f);
      ewind[1]=valparse<float>(*pars,"eww2",0.0f);
      ewind[2]=valparse<float>(*pars,"eww3",0.0f);
      width[0]=valparse<float>(*pars,"width0",0.0f);
      width[1]=valparse<float>(*pars,"width1",0.0f);
      width[2]=valparse<float>(*pars,"width2",0.0f);

      Vector<ireal> m_in(op.getDomain());
      AssignFilename minfn(valparse<std::string>(*pars,"csq"));
      Components<ireal> cmin(m_in);
      cmin[0].eval(minfn);
      GridMaskOp mop(op.getDomain(),m_in,swind,ewind,width);

      OperatorEvaluation<float> mopeval(mop,m_in);
      LinearOp<float> const & lmop=mopeval.getDeriv();

            // choice of preop is placeholder
            ScaleOpFwd<float> preop(op.getDomain(),1.0f);
            CGNEPolicyData<float> pd(valparse<float>(*pars,"ResidualTol",100.0*numeric_limits<float>::epsilon()),
                                     valparse<float>(*pars,"GradientTol",100.0*numeric_limits<float>::epsilon()),
                                     valparse<float>(*pars,"MaxStep",numeric_limits<float>::max()),
                                     valparse<int>(*pars,"MaxIter",10),true);
            // initial input reflectivity
            Vector<ireal> dm0(op.getDomain());
            string refname = valparse<std::string>(*pars,"ref0","");
            if (refname.size()>0){
                AssignFilename dmfn(refname);
                Components<ireal> cdm0(dm0);
                cdm0[0].eval(dmfn);
            }
            else{ dm0.zero(); }

      RPNT w_arr;
      RASN(w_arr,RPNT_1);
      w_arr[0]=valparse<float>(*pars,"scale1",1.0f);
      w_arr[1]=valparse<float>(*pars,"scale2",1.0f);
      IPNT sbc, ebc;
      sbc[0]=valparse<int>(*pars,"sbc0",1);
      sbc[1]=valparse<int>(*pars,"sbc1",0);
      sbc[2]=valparse<int>(*pars,"sbc2",0);
      ebc[0]=valparse<int>(*pars,"ebc0",0);
      ebc[1]=valparse<int>(*pars,"ebc1",0);
      ebc[2]=valparse<int>(*pars,"ebc2",0);
      float power=0.0f;
      float datum=0.0f;
      power=valparse<float>(*pars,"power",0.0f);
      datum=valparse<float>(*pars,"datum",0.0f);
 
      GridHelmFFTWOp hop(op.getDomain(),w_arr,sbc,ebc,power,datum);

      LinFitLSSM<float, CGNEPolicy<float>, CGNEPolicyData<float> > f(op,lmop,hop,mdd,dm0,pd,false,res);
            
            // lower, upper bds for csq
            Vector<float> lb(f.getDomain());
            Vector<float> ub(f.getDomain());
            float lbcsq=valparse<float>(*pars,"cmin");
            lbcsq=lbcsq*lbcsq;
            RVLAssignConst<float> asl(lbcsq);
            lb.eval(asl);
            float ubcsq=valparse<float>(*pars,"cmax");
            ubcsq=ubcsq*ubcsq;
            RVLAssignConst<float> asu(ubcsq);
            ub.eval(asu);
            RVLMin<float> mn;
#ifdef IWAVE_USE_MPI
            MPISerialFunctionObjectRedn<float,float> mpimn(mn);
            ULBoundsTest<float> ultest(lb,ub,mpimn);
#else
            ULBoundsTest<float> ultest(lb,ub,mn);
#endif
            
            FunctionalBd<float> fbd(f,ultest);
            
            // if gradient requested, compute and store, along with other quantities
            // at reference model
            string gradname = valparse<std::string>(*pars,"grad","");
            if (gradname.size()>0) {
                
                FunctionalEvaluation<float> Fm(fbd,m);
                // cerr<<" \n  compute gradient \n";
                AssignFilename gradfn(gradname);
                Vector<ireal> grad(dom);
                Components<ireal> cgrad(grad);
                cgrad[0].eval(gradfn);
                //strgrad << "\n getgradient norm = " << (Fm.getGradient()).norm() << endl;
                grad.copy(Fm.getGradient());
                //strgrad << "\n grad norm = " << grad.norm() << endl;
                
                Vector<ireal> dltm(op.getDomain());
                AssignFilename dltmfn(valparse<std::string>(*pars,"reflectivity"));
                Components<ireal> cdltm(dltm);
                cdltm[0].eval(dltmfn);
                dltm.zero();
                
                FunctionalBd<float> const & f1 =
                dynamic_cast<FunctionalBd<float> const &>(Fm.getFunctional()); // current clone of fbd

                LinFitLSSM<float, CGNEPolicy<float>, CGNEPolicyData<float> > const & f3 =
                dynamic_cast<LinFitLSSM<float, CGNEPolicy<float>, CGNEPolicyData<float> > const & >
                (f1.getFunctional()); // current clone of LinFitLSSM
                dltm.copy(f3.getLSSoln()); // copy dx from LinFitLSSM
                
                std::string dataest = valparse<std::string>(*pars,"dataest","");
                std::string datares = valparse<std::string>(*pars,"datares","");
                std::string normalres = valparse<std::string>(*pars,"normalres","");
                if (dataest.size()>0) {
                    OperatorEvaluation<float> opeval(op,m);
                    Vector<float> est(op.getRange());
                    AssignFilename estfn(dataest);
                    est.eval(estfn);
                    opeval.getDeriv().applyOp(dltm,est);
                    if (datares.size()>0) {
                        Vector<float> dres(op.getRange());
                        AssignFilename dresfn(datares);
                        dres.eval(dresfn);
                        dres.copy(est);
                        dres.linComb(-1.0f,mdd);
                    }
                }
            }
            
            // if model perturbation supplied, try scan, gradient tests
            string pertname = valparse<std::string>(*pars,"csq_d1","");
            if (pertname.size()>0) {
                
                Vector<ireal> dm(dom);
                AssignFilename mfn_d1(pertname);
                Components<ireal> cm_d1(dm);
                cm_d1[0].eval(mfn_d1);
                
                // perform gradient test if nhval > 0
                if(valparse<int>(*pars,"nhval",0)>0){
                    GradientTest(fbd,m,dm,strgrad,
                                 valparse<int>(*pars,"nhval",0),
                                 valparse<float>(*pars,"hmin",0.1f),
                                 valparse<float>(*pars,"hmax",1.0f));
                }
                
                // perform scan if nscan > -1
                if(valparse<int>(*pars,"nscan",-1)>-1){
                    Scan(fbd,m,dm,
                         valparse<int>(*pars,"nscan",-1),
                         valparse<float>(*pars,"hmin",-1.0f),
                         valparse<float>(*pars,"hmax",1.0f),
                         strgrad);	     
                }
            }
            if (retrieveRank() == 0) {
                std::string outfile = valparse<std::string>(*pars,"outfile","");
                if (outfile.size()>0) {
                    ofstream outf(outfile.c_str());
                    outf<<strgrad.str();
                    outf<<"\n ================================================= \n";
                    outf<<res.str();
                    outf.close();
                }
                else {
                    cout<<strgrad.str();
                    cout<<"\n ================================================= \n";
                    cout<<res.str();
                }
            }
            
#ifdef IWAVE_USE_MPI
        }
        MPI_Finalize();
#endif
    }
    catch (RVLException & e) {
        e.write(cerr);
#ifdef IWAVE_USE_MPI
        MPI_Abort(MPI_COMM_WORLD,0);
#endif
        exit(1);
    }
    
}
