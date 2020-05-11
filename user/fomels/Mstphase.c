/* Streaming estimate of instantaneous frequency. */
/*
  Copyright (C) 2004 University of Texas at Austin
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <math.h>

#include <rsf.h>

#include "stdiv.h"

int main (int argc, char* argv[])
{
    int nh, n1,n2, i1,i2, i, n12, dim, n[SF_MAX_DIM];
    float *trace, *hilb, *dtrace, *dhilb, *num, *den, *phase, a,b,c, mean, d1, lam;
    sf_complex *cnum, *cden, *crat, *ctrace;
    bool hertz, band, cmplx, verb;
    sf_file in, out;
	
    sf_init (argc,argv);
    in = sf_input("in");
    out = sf_output("out");
	
    if (SF_FLOAT   != sf_gettype(in) &&
	SF_COMPLEX != sf_gettype(in)) sf_error("Need float or complex input");
    sf_settype(out,SF_FLOAT);

    if (!sf_getfloat("lambda",&lam)) sf_error("Need lambda=");
    
    dim = sf_filedims (in,n);
    n1 = n[0];
    n12 = 1;
    for (i=0; i < dim; i++) {
	n12 *= n[i];
    }
    n2 = n12/n1;

    stdiv_init(n12,lam);

    if (!sf_getbool("verb",&verb)) verb=false;
    /* verbosity */
	
    if (!sf_histfloat(in,"d1",&d1)) d1=1.0f;
	
    trace = sf_floatalloc(n1);
    hilb = sf_floatalloc(n1);

    dtrace = sf_floatalloc(n1);
    dhilb = sf_floatalloc(n1);
	
    if (SF_COMPLEX == sf_gettype(in)) {
	cmplx = true;
    } else if (!sf_getbool("complex",&cmplx)) cmplx=false;
    /* if y, use complex-valued computations */
    
    if (cmplx) {
	ctrace = sf_complexalloc(n1);

	cnum = sf_complexalloc(n12);
	cden = sf_complexalloc(n12);
	crat = sf_complexalloc(n12);
	num = den = NULL;
    } else {
	num = sf_floatalloc(n12);
	den = sf_floatalloc(n12);
	ctrace = cnum = cden = crat = NULL;
    }
	
    phase = sf_floatalloc(n12);
	
    if (!sf_getint("order",&nh)) nh=100;
    /* Hilbert transformer order */
    if (!sf_getfloat("ref",&c)) c=1.;
    /* Hilbert transformer reference (0.5 < ref <= 1) */
	
    if (!sf_getbool("hertz",&hertz)) hertz=false;
    /* if y, convert output to Hertz */
	
    if (!sf_getbool("band",&band)) band=false;
    /* if y, compute instantaneous bandwidth */
	
    sf_hilbert_init(n1, nh, c);
    sf_deriv_init(n1, nh, c);
	
    mean=0.;
    for (i=i2=0; i2 < n2; i2++) {
	if (verb) sf_warning("slice %d of %d;",i2+1,n2);

	if (SF_COMPLEX != sf_gettype(in)) {
	    sf_floatread(trace,n1,in);
	    sf_hilbert(trace,hilb);
	} else {
	    sf_complexread(ctrace,n1,in);
	    for (i1=0; i1 < n1; i1++) {
		trace[i1] = crealf(ctrace[i1]);
		hilb[i1] = cimagf(ctrace[i1]);
	    }
	}
	
	if (band) {
	    for (i1=0; i1 < n1; i1++) {
		/* find envelope */
		trace[i1] = hypotf(trace[i1],hilb[i1]);
	    }
	    sf_deriv(trace,hilb);
	} else {
	    sf_deriv(trace,dtrace);
	    sf_deriv(hilb,dhilb);
	}
	
	if (cmplx) {
	    for (i1=0; i1 < nh; i1++, i++) {
		cnum[i] = sf_cmplx(0.,0.);
		cden[i] = sf_cmplx(0.,0.);
	    }	
	    
	    for (i1=nh; i1 < n1-nh; i1++, i++) {
		cnum[i] = sf_cmplx(dtrace[i1],dhilb[i1]);
		cden[i] = sf_cmplx( trace[i1], hilb[i1]);
		a = cabsf(cden[i]);
		mean += a*a;
	    }
	    
	    for (i1=n1-nh; i1 < n1; i1++, i++) {
		cnum[i] = sf_cmplx(0.,0.);
		cden[i] = sf_cmplx(0.,0.);
	    }
	} else {
	    for (i1=0; i1 < nh; i1++, i++) {
		num[i] = 0.;
		den[i] = 0.;
	    }	
	    
	    for (i1=nh; i1 < n1-nh; i1++, i++) {
		a = trace[i1];
		b = hilb[i1];
		if (band) {
		    num[i] = b;
		    den[i] = a;
		} else {
		    num[i] = a*dhilb[i1]-b*dtrace[i1];
		    den[i] = a*a+b*b;
		}
		mean += den[i]*den[i];
	    }
	    
	    for (i1=n1-nh; i1 < n1; i1++, i++) {
		num[i] = 0.;
		den[i] = 0.;
	    }
	} /* cmplx */
	
    } /* i2 */
    
    if (verb) sf_warning(".");
    
    mean = sqrtf(n12/mean);
    
    for (i=0; i < n12; i++) {
	if (cmplx) {
#ifdef SF_HAS_COMPLEX_H
	    cnum[i] *= mean;
	    cden[i] *= mean;  
#else
	    cnum[i] = sf_crmul(cnum[i],mean);
	    cden[i] = sf_crmul(cden[i],mean);
#endif
	} else {
	    num[i] *= mean;
	    den[i] *= mean;
	}
    }
	
    if (cmplx) {
	sf_error("complex not implemented yet");
    } else {
	stdiv(num,den,phase);
    }
	
    if (hertz) {
	/* convert to Hertz */    
	d1 = 1./(2.*SF_PI*d1);
	for (i=0; i < n12; i++) {
	    phase[i] *= d1;
	}
    }
	
    sf_floatwrite(phase,n12,out);
	
    exit(0);
}


