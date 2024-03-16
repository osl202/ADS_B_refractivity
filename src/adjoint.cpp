#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>

#include "adjoint.h"
#include "tracer.h"
#include "constants.h"

Tracer tracer;

std::default_random_engine gen(time(0));

Adjoint::Adjoint(const std::vector<double>& h0, const std::vector<double>& u0, const std::vector<double>& dmax,
                std::vector<double>& n, std::vector<double>& ndry, std::vector<double>& n_h, int argi)

        : h(h0), u(u0), d(dmax), size(h.size()), n_optim(n), n_optim_h(n_h), ndry(ndry), argi(argi)
        {}

void Adjoint::progressBackProp(int prog, int tot) {

    int width = 50;

    float percent = (float) prog / tot;
    int bar = (int) (percent * width);

    std::cout << "[";

    for (int i = 0; i < bar; ++i) {
        std::cout << "=";
    }

    for (int i = bar; i < width; ++i) {
        std::cout << " ";
    }

    std::cout << "] " << std::setw(3) << (int) (percent * 100) << "%\r backpropagating... ";
    
    std::cout.flush();
};

std::vector<double> Adjoint::retrieve(double obs_height, int n_iter, double lrate, double dr)

{   

    std::vector<double> cost_track(n_iter, 0.0);
    std::vector<double> m(n_optim.size(), 0.0);
    std::vector<double> v(n_optim.size(), 0.0);

    std::ostringstream filerms;
    filerms << "../RMS/PAPERII_RMS_NE_RK3_retrievals_t2700_3600.txt";
    std::ofstream rfilerms(filerms.str());

    for (int i(0); i < n_iter; i++)
    {   
     
        loss = 0.0;

        for (int jj(0); jj < size; jj++)
        {
            loss += pow((tracer.trace(obs_height, u[jj], d[jj], dr, n_optim, n_optim_h)[0] - h[jj]), 2);
        }
        cost_track[i] = loss;

        for (int j(0); j < size; j++)
        {

            progressBackProp(j, size);

            init_pos = tracer.trace(obs_height, u[j], d[j], dr, n_optim, n_optim_h);

            lam = 2*(init_pos[0] - h[j]);
            mu = 0.0;

            h_end = init_pos[0];
            u_end = -init_pos[1]; // reverse end direction of ray to initialise reverse pass
            d_end = init_pos[2];

            tracer.backprop(h_end, u_end, d_end, dr, n_optim, ndry, n_optim[0], n_optim_h, lam, mu, lrate, i, m ,v);        

        }


        std::cout << "\n" << "iteration: " << i << ' ' << "loss: " << loss << "\n" << std::endl;
        rfilerms << i << ' ' << loss << std::endl;

    }

    rfilerms.close();    

    return n_optim;


};

std::vector<double> Adjoint::retrieve_synthetic(double obs_height, int n_iter, double lrate, double dr, std::vector<double>& n_target, double noise)
{   

    std::cout << "Noise standard deviation: " << noise << "\n";

    std::cout << "Obs Height" << ' ' << obs_height << std::endl;

    target_pos.clear();

    std::normal_distribution<double> distribution (0.0,noise);

    for (int k(0); k < size; k++)
    {
        target_pos.push_back(tracer.trace(obs_height, u[k], d[k], dr, n_target, n_optim_h)[0]);
    }

    if (noise > 0.0)
    {   

        std::cout << "Noise added" << std::endl;

        std::ostringstream filenoise;
        filenoise << "../Noise/PAPERII_noise_NE_RK3_" << argi << "_" << noise << ".txt";
        std::ofstream rfilenoise(filenoise.str());

       double  AoArms = 0.0;

        for(int k(0); k < size; k++)
        {
            perturb = distribution(gen);

            AoA = asin(u[k]) + perturb*PI/180.0;

            AoArms += pow((asin(u[k]) - AoA)*180.0/PI, 2);

            u[k] = sin(AoA);

            rfilenoise << perturb << std::endl; 

        }

        std::cout << "AoA RMS: " << sqrt(AoArms/size) << std::endl;

        rfilenoise.close();

    }
    else if (noise < 0.0)
    {
        std::cerr << "Noise should be above or equal to 0.0 \n";
    }

    std::vector<double> m(n_optim.size(), 0.0);
    std::vector<double> v(n_optim.size(), 0.0);

    std::vector<double> cost_track(n_iter, 0.0);

    std::vector<double> n_prev;
    std::vector<double> n_grad;

    double criterion1, criterion2, criterion3;

    std::ostringstream filerms;
    filerms << "../RMS/PAPERII_RMS_NE_RK3_" << argi << "_" << noise << "2.txt";
    std::ofstream rfilerms(filerms.str());

    for (int i(0); i < n_iter; i++)
    {   

        loss_prev = 0.0;

        for (int jj(0); jj < size; jj++)
        {
            loss_prev += pow((tracer.trace(obs_height, u[jj], d[jj], dr, n_optim, n_optim_h)[0] - target_pos[jj]), 2);
        }

        n_prev = n_optim;

        n_rms = 0.0;

        for (int jjj(0); jjj < n_optim.size(); jjj++)
        {
            n_rms += pow((exp(n_optim[jjj]) - exp(n_target[jjj]))*1e6, 2);
        }

        for (int j(0); j < size; j++)
        {

            progressBackProp(j, size);

            init_pos = tracer.trace(obs_height, u[j], d[j], dr, n_optim, n_optim_h);

            lam = 2*(init_pos[0] - target_pos[j]);
            mu = 0.0;

            h_end = init_pos[0];
            u_end = -init_pos[1];
            d_end = init_pos[2];

            n_grad = tracer.backprop(h_end, u_end, d_end, dr, n_optim, ndry, n_target[0], n_optim_h, lam, mu, lrate, i, m, v);
        
        }

        loss = 0.0;

        for (int jj(0); jj < size; jj++)
        {
            loss += pow((tracer.trace(obs_height, u[jj], d[jj], dr, n_optim, n_optim_h)[0] - target_pos[jj]), 2);
        }

        n_loss = 0.0;
        n_sum = 0.0;
        n_grad_sum = 0.0;

        for (int ii(0); ii < (int) n_optim.size(); ii++)
        {
            n_loss += pow((exp(n_optim[ii]) - exp(n_prev[ii])), 2);
            n_sum += pow(exp(n_prev[ii]), 2);
            n_grad_sum += pow(n_grad[ii], 2);
        }

        criterion1 = sqrt(pow((sqrt(loss) - sqrt(loss_prev)),2)) / (1 + sqrt(loss_prev));
        criterion2 = sqrt(n_loss) / (1 + sqrt(n_sum));
        criterion3 = sqrt(n_grad_sum) / (1 + sqrt(loss_prev));

        

        std::cout << "\n" << "iteration: " << i << ' ' << "loss: " << loss_prev << ' ' << "RMS: " << sqrt(n_rms / n_optim.size()) << "\n" << std::endl;
        std::cout << "Criterion 1:  " << criterion1 << std::endl;
        std::cout << "Criterion 2:  " << criterion2 << std::endl;
        std::cout << "Criterion 3:  " << criterion3 << std::endl;

        rfilerms << i << ' ' << loss << ' ' << sqrt(n_rms / n_optim.size()) << std::endl;

        if ((criterion1 < 1e-2) && (criterion2 < 1e-7))// && (criterion3 <= 1e2))
        {
            std::cout << "Convergence at iteration " << i << ", stopping minimisation." << std::endl;
            std::cout << "Criterion 1:  " << criterion1 << std::endl;
            std::cout << "Criterion 2:  " << criterion2 << std::endl;
            std::cout << "Criterion 3:  " << criterion3 << std::endl;
            break;
        }

    }

    rfilerms.close();

    return n_optim;

};

std::vector<std::vector<double> > Adjoint::retrieve_paths(double obs_height, int n_iter, double lrate, double dr)
{   

    

    std::vector<double> flight_height0(size, 0.0);
    std::vector<double> flight_height1(size, 0.0);

    std::vector<double> n_prev;
    std::vector<double> n_grad;

    std::vector<double> m(n_optim.size(), 0.0);
    std::vector<double> v(n_optim.size(), 0.0);

    double criterion1, criterion2, criterion3;

    // Initial flight paths
    //-----------------------

    for (int jj(0); jj < size; jj++)
    {
        flight_height0[jj] = tracer.trace(obs_height, u[jj], d[jj], dr, n_optim, n_optim_h)[0];
    }

    for (int i(0); i < n_iter; i++)
    {   
     
        loss_prev = 0.0;

        for (int jj(0); jj < size; jj++)
        {
            loss_prev += pow((tracer.trace(obs_height, u[jj], d[jj], dr, n_optim, n_optim_h)[0] - h[jj]), 2);
        }

        n_prev = n_optim;

        for (int j(0); j < size; j++)
        {

            progressBackProp(j, size);

            init_pos = tracer.trace(obs_height, u[j], d[j], dr, n_optim, n_optim_h);

            lam = 2*(init_pos[0] - h[j]);
            mu = 0.0;

            h_end = init_pos[0];
            u_end = -init_pos[1];
            d_end = init_pos[2];

            n_grad = tracer.backprop(h_end, u_end, d_end, dr, n_optim, ndry, n_optim[0], n_optim_h, lam, mu, lrate, i, m, v);    
            
        }    

        loss = 0.0;

        for (int jj(0); jj < size; jj++)
        {
            loss += pow((tracer.trace(obs_height, u[jj], d[jj], dr, n_optim, n_optim_h)[0] - h[jj]), 2);
        }

        n_loss = 0.0;
        n_sum = 0.0;
        n_grad_sum = 0.0;

        for (int ii(0); ii < (int) n_optim.size(); ii++)
        {
            n_loss += pow((exp(n_optim[ii]) - exp(n_prev[ii])), 2);
            n_sum += pow(exp(n_prev[ii]), 2);
            n_grad_sum += pow(n_grad[ii], 2);
        }

        criterion1 = sqrt(pow((sqrt(loss) - sqrt(loss_prev)),2)) / (1 + sqrt(loss_prev));
        criterion2 = sqrt(n_loss) / (1 + sqrt(n_sum));
        criterion3 = sqrt(n_grad_sum) / (1 + sqrt(loss_prev));

        std::cout << "\n" << "iteration: " << i << ' ' << "loss: " << loss_prev << "\n" << std::endl;

        if ((criterion1 < 1e-2) && (criterion2 < 1e-7))// && (criterion3 <= 1e2))
        {
            std::cout << "Convergence at iteration " << i << ", stopping minimisation." << std::endl;
            std::cout << "Criterion 1:  " << criterion1 << std::endl;
            std::cout << "Criterion 2:  " << criterion2 << std::endl;
            std::cout << "Criterion 3:  " << criterion3 << std::endl;
            break;
        }

        
        // std::cout << "Criterion 1:  " << criterion1 << std::endl;
        // std::cout << "Criterion 2:  " << criterion2 << std::endl;
        // std::cout << "Criterion 3:  " << criterion3 << std::endl;

    }

    // Retrieved flight paths
    //-----------------------

    for (int jj(0); jj < size; jj++)
    {
        flight_height1[jj] = tracer.trace(obs_height, u[jj], d[jj], dr, n_optim, n_optim_h)[0];
    }

    std::vector<std::vector<double> > output;
    output.push_back(flight_height0);
    output.push_back(flight_height1);
    output.push_back(n_optim);

    return output;


};