#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <cstdio>

#include "tracer.h"
#include "constants.h"

std::vector<double> Tracer::trace(const double h0, const double u0, const double dmax, const double dr,
                                  std::vector<double>& n, std::vector<double>& n_h, bool forward)

//-------------------------------------------------------------------------------------------------------------
//
//  Ray tracing model based on the second-order differential equation (SODE) tracer developed by Zeng. et al
//  (2014).  The rays equations are integrated using a third-order Runge-Kutta scheme.
// 
//  Inputs:
//  +  h0                 : initial height of ray        
//  +  u0                 : initial direction of ray 
//  +  dmax               : initial distance of ray (aircraft distance across surface)
//  +  dr                 : ray step size
//  +  (vector) n         : refractive index field 
//  +  (vector) n_h       : height of refractive index levels
//  +  forward            : true if forward tracing, false if reverse tracing
//
//  Some variables:
//  +  s                  : distance travelled by ray across surface of Earth
//
//-------------------------------------------------------------------------------------------------------------

{
    steps = (int) 600/dr;


    h = h0;
    u = u0;

    if (forward)
    {
        s = 0.0;
        t = 1.0;
    }
    else
    {
        s = dmax;
        t = -1.0;
    }

    for(int i(0); i < steps; i++)
    { 
        r = h + EARTH_R;

        if(h < OBSERVER_H)
        {

            k1h = u;
            k2h = u + dr*k1u/2;
            k3h = u - dr*k1u + dr*2*k2u;
            
            k1u = (1 - u*u) * (0.0 + (1 / r));
            k2u = (1 - (u + dr*k1u/2)*(u + dr*k1u/2) )  * ( 0.0 +  1 / (r + dr*k1h/2)  );
            k3u = (1 - (u - dr*k1u + dr*2*k2u)*(u - dr*k1u + dr*2*k2u)) * ( 0.0 + 1 / (r - dr*k1h + dr*2*k2h));

        }
        else
        {
            {
            i_lev1 = std::upper_bound(n_h.begin(), n_h.end(), h);

            dw0_1 = - 1  / ( n_h[i_lev1 - n_h.begin()] - n_h[i_lev1 - 1 - n_h.begin()] );
            dw1_1 = -1 * dw0_1;
                
            n1 = dw0_1*n[i_lev1 - 1 - n_h.begin()] + dw1_1*n[i_lev1 - n_h.begin()]; 
            }

            k1h = u;
            k1u = (1 - u*u) * (n1 + (1 / r));

            // k2
            {
                i_lev2 = std::upper_bound(n_h.begin(), n_h.end(), (h + dr*k1h/2));

                dw0_2 = - 1  / ( n_h[i_lev2 - n_h.begin()] - n_h[i_lev2 - 1 - n_h.begin()] );
                dw1_2 = -1 * dw0_2;

                n2 = dw0_2*n[i_lev2 - 1 - n_h.begin()] + dw1_2*n[i_lev2 - n_h.begin()]; 
            }

            k2h = u + dr*k1u/2;
            k2u = (1 - (u + dr*k1u/2)*(u + dr*k1u/2) )  * ( n2 +  1 / (r + dr*k1h/2)  );

            // k3
            {
                i_lev3 = std::upper_bound(n_h.begin(), n_h.end(), (h - dr*k1h + dr*2*k2h));

                dw0_3 = - 1  / ( n_h[i_lev3 - n_h.begin()] - n_h[i_lev3 - 1 - n_h.begin()] );
                dw1_3 = -1 * dw0_3;

                n3 = dw0_3*n[i_lev3 - 1 - n_h.begin()] + dw1_3*n[i_lev3 - n_h.begin()];
            }

            k3h = u - dr*k1u + dr*2*k2u;
            k3u = (1 - (u - dr*k1u + dr*2*k2u)*(u - dr*k1u + dr*2*k2u)) * ( n3 + 1 / (r - dr*k1h + dr*2*k2h));

        }

        h = h + dr*(k1h + 4*k2h + k3h)/6;
        s = s + t*EARTH_R*asin(cos(asin(u))*dr / (EARTH_R + h));
        u = u + dr*(k1u + 4*k2u + k3u)/6;

       
        if(s >= dmax || s < 0)
        {
            final_pos = {h, u, s};

            break;
        }
    }

    return final_pos;

}

std::vector<double> Tracer::backprop(const double h0, const double u0, const double dmax, const double dr, std::vector<double>& n, std::vector<double>& ndry, const double n_init, std::vector<double>& n_h,
                      const double lam0, const double mu0, const double lrate, int iter, std::vector<double>& m, std::vector<double>& v, double* obs_height, int* index_n, double* dn_adj)

// -----------------------------------------------------------------------------------------------------
//
// Adjoint-based gradient descent.  Analagous to backpropagation in machine learning, the gradient of 
// the loss function is propagated through the refractivity field.  Initally the loss function is 
// differentiated with respect to the state variables h and u, before the full d L / d n is evaluated
// through the backwards ray tracing.  The gradients d L / d n1 and d L / d n0 are evaluated at each step,
// incrementing the refractivity values immediately above and below the ray position.  A third order Runge-
// Kutta scheme is used to integrate the rays.
//
// mu = d L / d u (= 0)
//
// lam = d L / d h
//
// then d L / d n_x (eval at r_i) = mu * (1 - u*u) * dw_x/dh where w is the interpolation weight:
//
// n(h, N) = w0*N0 + w1*N1
//
// Inputs:
//  +  h0                 : initial height of ray (aircraft altitude)         
//  +  u0                 : initial direction of ray (evaluated by -dir end point)
//  +  dmax               : initial distance of ray (aircraft distance across surface)
//  +  dr                 : ray step size
//  +  (vector) n         : refractive index field to be optimised
//  +  n_surface_true     : fixed field at surface
//  +  (vector) n_h       : height of refractive index levels
//  +  lam0               : initial adjoint associated with h
//  +  mu0                : initial adjoint associated with u
//  +  lrate              : learning rate - controls who far along gradient direction to go (careful!)
//  +  m & v              : first and second moments used in the psuedo-adam optimiser
//
// Some variables:
//  + dw0, etc            : the gradient of the weight used in the linear interpolation scheme. E.g. 
//                          n_eval = N0*(y1 - y)/(y1 - y0) + N1*(y - y0)/(y1 - y0), there
//                          dndh_eval = N0 * -1/(y1 - y0) + N1 * 1/(y1 - y0)
//  + k1,2,etc            : used for Runge-Kutta integration, y = y + (k1 + 4*k2 + k3)/6
//
//  + s                   : distance across surface travelled by ray
//
//------------------------------------------------------------------------------------------------------

{
    steps = (int) 600/dr;

    h = h0;
    u = u0;
    s = dmax;

    mu = mu0;
    lam = lam0;

    if(obs_height != nullptr){
        h_obs = *obs_height;
    }
    else{
        h_obs = OBSERVER_H;
    }

    std::vector<double> ngrad((int) n.size(), 0.0);

    for(int i(0); i < steps; i++)
    { 
        r = h + EARTH_R;

        if(h < OBSERVER_H)
        {

            k1h = u;
            k2h = u + dr*k1u/2;
            k3h = u - dr*k1u + dr*2*k2u;
            
            k1u = (1 - u*u) * (0.0 + (1 / r));
            k2u = (1 - (u + dr*k1u/2)*(u + dr*k1u/2) )  * ( 0.0 +  1 / (r + dr*k1h/2)  );
            k3u = (1 - (u - dr*k1u + dr*2*k2u)*(u - dr*k1u + dr*2*k2u)) * ( 0.0 + 1 / (r - dr*k1h + dr*2*k2h));


            k1lam = 0.0;//dr*( mu * (1 - u*u) * ( 0.0 + 1 / (r*r) ) );
            k2lam = k1lam;
            k3lam = k1lam;

            k1mu = 0.0;//dr*( -lam + 2 * mu * u * (0.0 + (1 / r)));
            k2mu = k1mu;
            k3mu = k1mu;
            
        }

        else
        {
            // k1 
            {
                i_lev1 = std::upper_bound(n_h.begin(), n_h.end(), h);

                dw0_1 = - 1  / ( n_h[i_lev1 - n_h.begin()] - n_h[i_lev1 - 1 - n_h.begin()] );
                dw1_1 = -1 * dw0_1;
                    
                n1 = dw0_1*n[i_lev1 - 1 - n_h.begin()] + dw1_1*n[i_lev1 - n_h.begin()]; 
            }

            k1h = u;
            k1u = (1 - u*u) * (n1 + (1 / r));

            // k2
            {
                i_lev2 = std::upper_bound(n_h.begin(), n_h.end(), (h + dr*k1h/2));

                dw0_2 = - 1  / ( n_h[i_lev2 - n_h.begin()] - n_h[i_lev2 - 1 - n_h.begin()] );
                dw1_2 = -1 * dw0_2;

                n2 = dw0_2*n[i_lev2 - 1 - n_h.begin()] + dw1_2*n[i_lev2 - n_h.begin()]; 
            }

            k2h = u + dr*k1u/2;
            k2u = (1 - (u + dr*k1u/2)*(u + dr*k1u/2) )  * ( n2 +  1 / (r + dr*k1h/2)  );

            // k3
            {
                i_lev3 = std::upper_bound(n_h.begin(), n_h.end(), (h - dr*k1h + dr*2*k2h));

                dw0_3 = - 1  / ( n_h[i_lev3 - n_h.begin()] - n_h[i_lev3 - 1 - n_h.begin()] );
                dw1_3 = -1 * dw0_3;

                n3 = dw0_3*n[i_lev3 - 1 - n_h.begin()] + dw1_3*n[i_lev3 - n_h.begin()];
            }

            k3h = u - dr*k1u + dr*2*k2u;
            k3u = (1 - (u - dr*k1u + dr*2*k2u)*(u - dr*k1u + dr*2*k2u)) * ( n3 + 1 / (r - dr*k1h + dr*2*k2h));

            // k lam and mu
            
            k1lam = mu * (1 - u*u) * ( n1*n1 + (1 / (r*r)) );
            k1mu = -lam + 2 * mu * u * (n1 + (1 / r));

            k2lam = (mu + dr*k1mu/2) * (1 - (u + dr*k1u/2)*(u + dr*k1u/2) ) * ( n2*n2 + (1 / ( (r + dr*k1h/2)*(r + dr*k1h/2) )) );
            k2mu = -(lam + dr*k1lam/2) + 2 * (mu + dr*k1mu/2) * (u + dr*k1u/2) * ( n2 + (1 / (r + dr*k1h/2)) );

            k3lam = (mu - dr*k1mu + dr*2*k2mu) * (1 - (u - dr*k1u + dr*2*k2u)*(u - dr*k1u + dr*2*k2u) )  * ( n3*n3 + (1 / ( (r - dr*k1h + dr*2*k2h)*(r - dr*k1h + dr*2*k2h) )) );
            k3mu = -(lam - dr*k1lam + dr*2*k2lam) + 2 * (mu - dr*k1mu + dr*2*k2mu) * (u - dr*k1u + dr*2*k2u) * (n3 + (1 / (r - dr*k1h + dr*2*k2h)) );
        
        }
        
        // Evaluate gradients for upper and lower refractivity values
        //-----------------------------------------------------------

        dn0 = mu*(u*u - 1)*dw0_1;
        dn1 = mu*(u*u - 1)*dw1_1;

        if (index_n != nullptr && dn_adj != nullptr)
        {

            if(n[i_lev1 - 1 - n_h.begin()] == n[*index_n])
            {
                *dn_adj += dn0*dr / exp(n[i_lev1 - 1 - n_h.begin()]);
            }

            if(n[i_lev1 - n_h.begin()] == n[*index_n])
            {
                *dn_adj += dn1*dr / exp(n[i_lev1 - n_h.begin()]);
            }

        }

        // Update respective refractivity values using gradient descent
        //-------------------------------------------------------------

        h = h + dr*(k1h + 4*k2h + k3h)/6;
        s = s - EARTH_R*asin(cos(asin(u))*dr / (EARTH_R + h));
        u = u + dr*(k1u + 4*k2u + k3u)/6;

        mu = mu + dr*(k1mu + 4*k2mu + k3mu)/6;
        lam = lam + dr*(k1lam + 4*k2lam + k3lam)/6;

        if( s <= 1e-6 || h <= h_obs || h >= n_h.back())
        {   
            break;
        }

        if (index_n == nullptr && dn_adj == nullptr)
        {      

            // if(n_h[i_lev1 - 1 - n_h.begin()] > 6){

            //     dn0 += 1e3*2*(n[i_lev1 - 1 - n_h.begin()] - ndry[i_lev1 - 1 - n_h.begin()]);
            //     dn1 += 1e3*2*(n[i_lev1 - n_h.begin()] - ndry[i_lev1 - n_h.begin()]);

            // }

            ngrad[i_lev1 - 1 - n_h.begin()] += dn0;
            ngrad[i_lev1 - n_h.begin()] += dn1;
            
        }
    }

    for(int i(0); i < (int) n.size(); i++)
    {

        // Adam optimiser
        //---------------

        m[i] = beta1*m[i] + (1.0-beta1)*ngrad[i];
        v[i] = beta2*v[i] + (1.0-beta2)*ngrad[i]*ngrad[i];

        m_est = m[i] / (1.0 - pow(beta1, (iter+1)));
        v_est = v[i] / (1.0 - pow(beta2, (iter+1)));

        
        n[i] -= lrate*m_est / sqrt(v_est + epsilon);
    
        // if (n_h[i] > 8)
        // {
        //     n[i] = ndry[i];
        // }

        if(n[i] < ndry[i]){n[i] = ndry[i];}
    }

    n[0] = n_init; // Clamp surface refractivity
        
    return ngrad;
};

std::vector<std::vector<double>> Tracer::trace_paths(const double h0, const double u0,const double dmax, const double dr,
                                                 std::vector<double>& n, std::vector<double>& n_h, bool forward)
{
    steps = (int) 600/dr;


    h = h0;
    u = u0;

    if (forward)
    {
        s = 0.0;
        t = 1.0;
    }
    else
    {
        s = dmax;
        t = -1.0;
    }

    std::vector<double> vals;
    paths.clear();

    for(int i(0); i < steps; i++)
    { 

        vals = {s, h, u};
        paths.push_back(vals);
    
        r = h + EARTH_R;

        i_lev1 = std::upper_bound(n_h.begin(), n_h.end(), h);

        dw0_1 = - 1  / ( n_h[i_lev1 - n_h.begin()] - n_h[i_lev1 - 1 - n_h.begin()] );
        dw1_1 = -1 * dw0_1;
            
        n1 = dw0_1*n[i_lev1 - 1 - n_h.begin()] + dw1_1*n[i_lev1 - n_h.begin()]; 


        k1h = dr*u;
        k1u = dr*( (1 - u*u) * (n1 + (1 / r)) );

        i_lev2 = std::upper_bound(n_h.begin(), n_h.end(), (h + k1h/2));

        dw0_2 = - 1  / ( n_h[i_lev2 - n_h.begin()] - n_h[i_lev2 - 1 - n_h.begin()] );
        dw1_2 = -1 * dw0_2;

        n2 = dw0_2*n[i_lev2 - 1 - n_h.begin()] + dw1_2*n[i_lev2 - n_h.begin()]; 

        
        k2h = dr*(u + k1u/2);
        k2u = dr*( (1 - (u + k1u/2)*(u + k1u/2))  * ( n2 + 1 / (r + k1h/2)));

        i_lev3 = std::upper_bound(n_h.begin(), n_h.end(), (h - k1h + 2*k2h));

        dw0_3 = - 1  / ( n_h[i_lev3 - n_h.begin()] - n_h[i_lev3 - 1 - n_h.begin()] );
        dw1_3 = -1 * dw0_3;

        n3 = dw0_3*n[i_lev3 - 1 - n_h.begin()] + dw1_3*n[i_lev3 - n_h.begin()];


        k3h = dr*(u - k1u + 2*k2u);
        k3u = dr*( (1 - (u - k1u + 2*k2u)*(u - k1u + 2*k2u)) * ( n3 + 1 / (r - k1h + 2*k2h)));

        h = h + (k1h + 4*k2h + k3h)/6;
        s = s + t*EARTH_R*asin(cos(asin(u))*dr / (EARTH_R + h));
        u = u + (k1u + 4*k2u + k3u)/6;

        if(s > dmax || s < 0)
        {
            break;
        }
    }

    return paths;

}

std::vector<double> Tracer::backprop_grad(const double h0, const double u0, const double dmax, const double dr, std::vector<double>& n, std::vector<double>& ndry, const double n_init, std::vector<double>& n_h,
                      const double lam0, const double mu0, const double lrate, int iter, std::vector<double>& m, std::vector<double>& v, double* obs_height, int* index_n, double* dn_adj)

// -----------------------------------------------------------------------------------------------------
//
// Adjoint-based gradient descent.  Analagous to backpropagation in machine learning, the gradient of 
// the loss function is propagated through the refractivity field.  Initally the loss function is 
// differentiated with respect to the state variables h and u, before the full d L / d n is evaluated
// through the backwards ray tracing.  The gradients d L / d n1 and d L / d n0 are evaluated at each step,
// incrementing the refractivity values immediately above and below the ray position.  A third order Runge-
// Kutta scheme is used to integrate the rays.
//
// mu = d L / d u (= 0)
//
// lam = d L / d h
//
// then d L / d n_x (eval at r_i) = mu * (1 - u*u) * dw_x/dh where w is the interpolation weight:
//
// n(h, N) = w0*N0 + w1*N1
//
// Inputs:
//  +  h0                 : initial height of ray (aircraft altitude)         
//  +  u0                 : initial direction of ray (evaluated by -dir end point)
//  +  dmax               : initial distance of ray (aircraft distance across surface)
//  +  dr                 : ray step size
//  +  (vector) n         : refractive index field to be optimised
//  +  n_surface_true     : fixed field at surface
//  +  (vector) n_h       : height of refractive index levels
//  +  lam0               : initial adjoint associated with h
//  +  mu0                : initial adjoint associated with u
//  +  lrate              : learning rate - controls who far along gradient direction to go (careful!)
//  +  m & v              : first and second moments used in the psuedo-adam optimiser
//
// Some variables:
//  + dw0, etc            : the gradient of the weight used in the linear interpolation scheme. E.g. 
//                          n_eval = N0*(y1 - y)/(y1 - y0) + N1*(y - y0)/(y1 - y0), there
//                          dndh_eval = N0 * -1/(y1 - y0) + N1 * 1/(y1 - y0)
//  + k1,2,etc            : used for Runge-Kutta integration, y = y + (k1 + 4*k2 + k3)/6
//
//  + s                   : distance across surface travelled by ray
//
//------------------------------------------------------------------------------------------------------

{
    steps = (int) 600/dr;

    h = h0;
    u = u0;
    s = dmax;

    mu = mu0;
    lam = lam0;

    if(obs_height != nullptr){
        h_obs = *obs_height;
    }
    else{
        h_obs = OBSERVER_H;
    }

    std::vector<double> ngrad((int) n.size(), 0.0);

    for(int i(0); i < steps; i++)
    { 
        r = h + EARTH_R;

        {
            // k1 
            {
                i_lev1 = std::upper_bound(n_h.begin(), n_h.end(), h);

                w0_1 = ( n_h[i_lev1 - n_h.begin()] - h )  / ( n_h[i_lev1 - n_h.begin()] - n_h[i_lev1 - 1 - n_h.begin()] );
                w1_1 = ( h - n_h[i_lev1 - 1 - n_h.begin()] )  / ( n_h[i_lev1 - n_h.begin()] - n_h[i_lev1 - 1 - n_h.begin()] );
                    
                n1 = dw0_1*n[i_lev1 - 1 - n_h.begin()] + dw1_1*n[i_lev1 - n_h.begin()]; 
            }

            k1h = dr*u;
            k1u = dr*( (1 - u*u) * (n1 + (1 / r)) );

            // k2
            {
                i_lev2 = std::upper_bound(n_h.begin(), n_h.end(), (h + k1h/2));

                w0_2 = ( n_h[i_lev2 - n_h.begin()] - h )  / ( n_h[i_lev2 - n_h.begin()] - n_h[i_lev2 - 1 - n_h.begin()] );
                w1_2 = ( h - n_h[i_lev2 - 1 - n_h.begin()] )  / ( n_h[i_lev2 - n_h.begin()] - n_h[i_lev2 - 1 - n_h.begin()] );

                n2 = w0_2*n[i_lev2 - 1 - n_h.begin()] + w1_2*n[i_lev2 - n_h.begin()]; 
            }

            k2h = dr*(u + k1u/2);
            k2u = dr*( (1 - (u + k1u/2)*(u + k1u/2) )  * ( n2 +  1 / (r + k1h/2)  ) );

            // k3
            {
                i_lev3 = std::upper_bound(n_h.begin(), n_h.end(), (h - k1h + 2*k2h));

                w0_3 = ( n_h[i_lev3 - n_h.begin()] - h )  / ( n_h[i_lev3 - n_h.begin()] - n_h[i_lev3 - 1 - n_h.begin()] );
                w1_3 = ( h - n_h[i_lev3 - 1 - n_h.begin()] )  / ( n_h[i_lev3 - n_h.begin()] - n_h[i_lev3 - 1 - n_h.begin()] );

                n3 = w0_3*n[i_lev3 - 1 - n_h.begin()] + w1_3*n[i_lev3 - n_h.begin()];
            }

            k3h = dr*(u - k1u + 2*k2u);
            k3u = dr*( (1 - (u - k1u + 2*k2u)*(u - k1u + 2*k2u)) * ( n3 + 1 / (r - k1h + 2*k2h)));

            // k lam and mu
            
            k1lam = dr*( mu * (1 - u*u) * ( n1*n1 + 1 / (r*r) ) );
            k1mu = dr*( -lam + 2 * mu * u * (n1 + (1 / r)));

            k2lam = dr*( (mu + k1mu/2) * (1 - (u + k1u/2)*(u + k1u/2) ) * ( n2*n2 + 1 / ( (r + k1h/2)*(r + k1h/2) ) ) );
            k2mu = dr*( -(lam + k1lam/2) + 2 * (mu + k1mu/2) * (u + k1u/2) * ( n2 + 1 / (r + k1h/2) ) );

            k3lam = dr*( (mu - k1mu + 2*k2mu) * (1 - (u - k1u + 2*k2u)*(u - k1u + 2*k2u) )  * ( n3*n3 + 1 / ( (r - k1h + 2*k2h)*(r - k1h + 2*k2h) ) ) );
            k3mu = dr*( -(lam - k1lam + 2*k2lam) + 2 * (mu - k1mu + 2*k2mu) * (u - k1u + 2*k2u) * (n3 + 1 / (r - k1h + 2*k2h) ) );
        
        }
        
        // Evaluate gradients for upper and lower refractivity values
        //-----------------------------------------------------------

        dn0 = mu*(u*u - 1)*dw0_1;
        dn1 = mu*(u*u - 1)*dw1_1;

        if (index_n != nullptr && dn_adj != nullptr)
        {

            if(n[i_lev1 - 1 - n_h.begin()] == n[*index_n])
            {
                *dn_adj += dn0*dr / exp(n[i_lev1 - 1 - n_h.begin()]);
            }

            if(n[i_lev1 - n_h.begin()] == n[*index_n])
            {
                *dn_adj += dn1*dr / exp(n[i_lev1 - n_h.begin()]);
            }

        }

        // Update respective refractivity values using gradient descent
        //-------------------------------------------------------------

        h = h + (k1h + 4*k2h + k3h)/6;
        s = s - EARTH_R*asin(cos(asin(u))*dr / (EARTH_R + h));
        u = u + (k1u + 4*k2u + k3u)/6;

        mu = mu + (k1mu + 4*k2mu + k3mu)/6;
        lam = lam + (k1lam + 4*k2lam + k3lam)/6;

        if( s <= 1e-6 || h <= h_obs || h >= OBSERVER_H + exp(2.7*29/30) - 1.0)
        {   
            break;
        }

        if (index_n == nullptr && dn_adj == nullptr)
        {      

            // if(n_h[i_lev1 - 1 - n_h.begin()] > 6){

            //     dn0 += 1e3*2*(n[i_lev1 - 1 - n_h.begin()] - ndry[i_lev1 - 1 - n_h.begin()]);
            //     dn1 += 1e3*2*(n[i_lev1 - n_h.begin()] - ndry[i_lev1 - n_h.begin()]);

            // }

            ngrad[i_lev1 - 1 - n_h.begin()] += dn0;
            ngrad[i_lev1 - n_h.begin()] += dn1;
            
        }
    }

    for(int i(0); i < (int) n.size(); i++)
    {

        // Adam optimiser
        //---------------

        m[i] = beta1*m[i] + (1.0-beta1)*ngrad[i];
        v[i] = beta2*v[i] + (1.0-beta2)*ngrad[i]*ngrad[i];

        m_est = m[i] / (1.0 - pow(beta1, (iter+1)));
        v_est = v[i] / (1.0 - pow(beta2, (iter+1)));

        
        n[i] -= lrate*m_est / sqrt(v_est + epsilon);

        //if(n[i] < ndry[i]){n[i] = ndry[i];}
    }

    n[0] = n_init; // Clamp surface refractivity
        
    return ngrad;
};