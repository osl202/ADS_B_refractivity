#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <cstdio>

#include "tracer.h"
#include "inputs.h"
#include "adjoint.h"
#include "constants.h"

#include "spline.h"


double n_lev = 30;

std::vector<double> n_h(n_lev, 0);
std::vector<double> logn(n_lev, 0);
std::vector<double> logn_init(n_lev, 0);
std::vector<double> logn_target(n_lev, 0);


int main()
{

    // Read in refractivity data

    Atmosphere atmosphere_input("Refractivity_data/22Sep_12z_Watnall_profile.txt");
    atmosphere_input.process();

    const std::vector<double>& N_profile = atmosphere_input.N();
    const std::vector<double>& h_profile = atmosphere_input.H();

    // Read in ADS-B data

    ADSB adsb_input("ADS_B_data/sep_NE_paperII_input_5.000_central10.txt", 1000);
    adsb_input.process();

    const std::vector<double> u_adsb = adsb_input.sin_obsAoA();
    const std::vector<double> o_adsb = adsb_input.obsAoA();
    const std::vector<double> h_adsb = adsb_input.rH();
    const std::vector<double> d_adsb = adsb_input.rD();
   
    Tracer rayTracer;

    tk::spline interp_n(h_profile,N_profile,tk::spline::cspline,true);

    for(int i(0); i < n_lev; i++)
    {

        n_h[i] = OBSERVER_H + exp(2.7*i/n_lev) - 1.0;

        logn[i] = log(1.000 + interp_n(n_h[0])*exp(-(n_h[i] - OBSERVER_H)/8.0)/1e6);

        logn_init[i] = log(1.000 + interp_n(n_h[0])*exp(-(n_h[i] - OBSERVER_H)/8.0)/1e6);

        logn_target[i] = log(1.000 + interp_n(n_h[i])/1e6);

    }

    Adjoint Profile(h_adsb, u_adsb, d_adsb, logn, n_h);

    // Tracing and learning parameters
    double dr = 0.1;
    double learn_rate = 5e-11;
    int iterations = 30;
    double noise_std = 0.0;//0.01;

    std::vector<double> retrieval = Profile.retrieve_synthetic(OBSERVER_H,iterations, learn_rate, dr, logn_target, noise_std);

    std::ostringstream file;
    file << "PAPERII_retrieve_NE_RK3_NEW_0.00.txt";
    std::ofstream rfile(file.str());

    for(int i(0); i < n_lev; i++)
    {
    rfile << (exp(retrieval[i]) - 1)*1e6 << ' ' << n_h[i] << ' '  << (exp(logn_target[i]) - 1)*1e6 << ' ' << (exp(logn_init[i]) - 1)*1e6 << std::endl;
    }

    rfile.close();

    return 0;
}

