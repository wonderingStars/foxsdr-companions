#define _USE_MATH_DEFINES
#include "fftw3.h"
#include <cstdio>
#include <complex>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
int main(){
  int ns[]={1,2,3,5,7,8,12,13,91,112,160,256,288,1024};
  double worst=0;
  for(int n:ns){
    std::vector<double> in(2*n),out(2*n);
    for(int i=0;i<n;i++){in[2*i]=sin(i*0.37)+0.1*(i%3);in[2*i+1]=cos(i*1.3);}
    for(int s:{-1,1}){
      fftw_plan p=fftw_plan_dft_1d(n,(fftw_complex*)in.data(),(fftw_complex*)out.data(),s,FFTW_ESTIMATE);
      fftw_execute(p);fftw_destroy_plan(p);
      for(int k=0;k<n;k++){std::complex<double> a=0;for(int j=0;j<n;j++)a+=std::complex<double>(in[2*j],in[2*j+1])*std::polar(1.0,s*2*M_PI*j*k/n);
        worst=std::max(worst,std::abs(a-std::complex<double>(out[2*k],out[2*k+1])));}
    }
    // r2r
    std::vector<double> r(n),h(n),b(n);
    for(int i=0;i<n;i++)r[i]=in[2*i];
    fftw_plan f=fftw_plan_r2r_1d(n,r.data(),h.data(),FFTW_R2HC,FFTW_ESTIMATE);fftw_execute(f);
    for(int k=0;k<=n/2;k++){std::complex<double> a=0;for(int j=0;j<n;j++)a+=r[j]*std::polar(1.0,-2*M_PI*j*k/n);
      worst=std::max(worst,std::abs(a.real()-h[k]));if(k&&k<(n+1)/2)worst=std::max(worst,std::abs(a.imag()-h[n-k]));}
    fftw_plan g=fftw_plan_r2r_1d(n,h.data(),b.data(),FFTW_HC2R,FFTW_ESTIMATE);fftw_execute(g);
    for(int i=0;i<n;i++)worst=std::max(worst,std::abs(b[i]/n-r[i]));
  }
  printf("worst err %g\n",worst);
}
