#!/bin/bash -l
export APP_NAME=madagascar
export VER=git
export SOFTWARE_DIR=$PWD
mkdir -p ${SOFTWARE_DIR}/src ${SOFTWARE_DIR}/build ${SOFTWARE_DIR}/apps ${SOFTWARE_DIR}/modulefiles/${APP_NAME}
cd ${SOFTWARE_DIR}/build
export INSTALL_DIR=${SOFTWARE_DIR}/apps/${APP_NAME}/${VER}
echo $APP_NAME/$VER


function get_source() {
	if [ ${VER} == "git" ]
	 then
		cd ${SOFTWARE_DIR}/build
	        git clone https://github.com/pplotn/RSFSRC.git
	else
	  a=1
#		wget https://sourceforge.net/projects/rsf/files/latest/download -O ${SOFTWARE_DIR}/src/madagascar-devel.tar.gz
	fi
}

function build_app() {
cd ${SOFTWARE_DIR}/build
if [ ${VER} == "git" ]
 then
	cd RSFSRC
 else
	tar xvf ../src/madagascar-devel.tar.gz
        cd madagascar-3.0
fi
module use /sw/csgv/modulefiles/compilers
module use /sw/csi/modulefiles/libs 
module use /sw/csi/modulefiles/compilers
module load gcc/6.4.0
# module load gcc/8.2.0 
module load fftw/3.3.7/openmpi-3.0.0-gcc-6.4.0-sp
module load fftw/3.3.7/openmpi-3.0.0-gcc-6.4.0-sp

module load swig/4.0.1 lapack/3.9.0 cuda/10.0.130
module load python/3.7.0
echo "Running sed on SConstruct"
sed -i -e "s;^ *env *= *Environment( *);ENV=dict()\nfor i in os.environ:\n   ENV[i]=os.environ[i]\nenv = Environment(ENV=ENV)\nenv['CC']='gcc'\nenv['CXX']='g++'\nenv['F90'] = 'gfortran'\nenv['F77']='gfortran'\nenv['FORTRAN']='gfortran'\nenv['MPICC']='mpicc'\nenv['MPICXX']='mpicxx'\nenv['MPIF90']='mpif90'\nenv['API'] = 'python,C++'\nenv['MPIRUN']='mpirun'\nenv['CUDA_TOOLKIT_PATH']=os.getenv('CUDATOOLKIT_HOME')\nenv['CPPFLAGS']='-I'+os.getenv('LAPACK_HOME')+'/include'+' -I'+os.getenv('FFTW_DIR')+'/include'\nenv['LIBPATH']=[os.path.join(os.getenv('LAPACK_HOME'),'lib64'),os.path.join(os.getenv('FFTW_DIR'),'lib')]\nenv['LIBS']=['lapack','blas','cblas','fftw3f'];g" SConstruct
sed -i 's/string.rfind(cc,/cc.rfind(/g' ./user/chenyk/SConstruct
./configure --prefix=${INSTALL_DIR} 

make -j 16 VERBOSE=1
}

function install_app() {
make install
}

function make_modulefile() {
echo 
cat << EOF > ${SOFTWARE_DIR}/modulefiles/${APP_NAME}/${VER}
#%Module

module-whatis "Madagascar"

proc ModulesHelp {} {
puts stderr " 
Madagascar is an open-source software package for multidimensional
data analysis and reproducible computational experiments. Its
mission is to provide:
  - a convenient and powerful environment
  - a convenient technology transfer tool

for researchers working with digital image and data processing in
geophysics and related fields. Technology developed using the
Madagascar project management system is transferred in the form of
recorded processing histories, which become computational recipes to
be verified, exchanged, and modified by users of the system.

* http://http://www.ahay.org/wiki/Main_Page"
}
module use /sw/csgv/modulefiles/compilers
module use /sw/csi/modulefiles/libs 
module use /sw/csi/modulefiles/compilers
module load /sw/csi/modulefiles/compilers/gcc/6.4.0
module load /sw/csi/modulefiles/compilers/openmpi/3.0.0/gcc-6.4.0 
module load /sw/csi/modulefiles/libs/fftw/3.3.7/openmpi-3.0.0-gcc-6.4.0-sp
module load /sw/csi/modulefiles/libs/swig/4.0.1/gnu-6.4.0 
module load /sw/csi/modulefiles/libs/lapack/3.9.0/gnu-6.4.0
module load /sw/csgv/modulefiles/compilers/cuda/10.0.130
set app 	$APP_NAME
set ver 	$VER

set loaded_PrgEnv 0
set build 0

#puts stderr "$whatis"
##module-whatis $whatis


set app_dir 	$INSTALL_DIR

setenv MADAGASCAR_HOME \${app_dir}

# Path for Madagascar installation directory
setenv RSFROOT \${app_dir}
# Path for Madagascar source directory
setenv RSFSRC \${app_dir}
# Path for binary data files part of RSF datasets
setenv DATAPATH /var/tmp/



prepend-path PATH \${app_dir}/bin
prepend-path MANPATH \${app_dir}/share/man
prepend-path LD_LIBRARY_PATH \${app_dir}/lib
append-path INCLUDE \${app_dir}/include
prepend-path PYTHONPATH \${app_dir}/lib/python3.7/site-packages

module load swig
EOF
}

if [ -z $1 ] 
	then
	get_source
	build_app
	install_app
	make_modulefile
else 
	ARG=$1
	if [ ${ARG} = "compile" ]
 		then
			echo "Will build Madagascar in $SOFTWARE/build"
			build_app

	elif [ ${ARG} = "install" ] 
		then
			echo "Will install Madagascar in $SOFTWARE_DIR/apps"
			install_app
	elif [ ${ARG} = "modulefile" ]
		then
			echo "Will creat modulefile in $SOFTWARE_DIR/modulefiles"
			make_modulefile
	fi
fi
