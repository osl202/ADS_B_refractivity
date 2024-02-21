#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>

#include "inputs.h"

Atmosphere::Atmosphere(const std::string& file)
    : file(file) {}

void Atmosphere::process() 
{

    std::ifstream inputFile(file);

    if (!inputFile.is_open())
    {
        std::cerr << "Error: Unable to open refractivity profile file " << file << std::endl;
        return;
    }

    double h, n, ndry, nwet;

    while (inputFile >> h >> n >> ndry >> nwet)
    {
        H_input.push_back(h*1e-3);
        N_input.push_back(n);
        NDRY_input.push_back(ndry);
        NWET_input.push_back(nwet);

    }

    inputFile.close();
}

const std::vector<double>& Atmosphere::H() const {
    return H_input;
}
const std::vector<double>& Atmosphere::N() const {
    return N_input;
}
const std::vector<double>& Atmosphere::NDRY() const {
    return NDRY_input;
}
const std::vector<double>& Atmosphere::NWET() const {
    return NWET_input;
}

ADSB::ADSB(const std::string& file, int length)
    : file(file), length(length) {

    std::cout << "Number of observations: " << length << std::endl;

}

void ADSB::process() 
{

    std::ifstream inputFile(file);

    if (!inputFile.is_open())
    {
        std::cerr << "Error: Unable to open ADS-B data file " << file << std::endl;
        return;
    }

    double obsaoa, h, d, repaoa, azi, t;

    while (inputFile >> obsaoa >> h >> d >> repaoa >> azi >> t && (obsAOA_input.size() < length))
    {
        obsAOA_input.push_back(obsaoa);
        sin_obsAOA_input.push_back(sin(obsaoa*pi/180.0));
        rH_input.push_back(h);
        rD_input.push_back(d);
        repAOA_input.push_back(repaoa);
        azim_input.push_back(azi);
        time_input.push_back(t);

    }

    // while (inputFile >> obsaoa >> repaoa >> h >> d && (sin_obsAOA_input.size() < length))
    // {
    //     //obsAOA_input.push_back(obsaoa);
    //     sin_obsAOA_input.push_back(sin(obsaoa*pi/180.0));
    //     rH_input.push_back(h);
    //     rD_input.push_back(d);
    //     repAOA_input.push_back(repaoa);
    //     //azim_input.push_back(azi);
    //     //time_input.push_back(t);

    // }

    inputFile.close();
} 

const std::vector<double>& ADSB::obsAoA() const {
    return obsAOA_input;
}
const std::vector<double>& ADSB::sin_obsAoA() const {
    return sin_obsAOA_input;
}
const std::vector<double>& ADSB::rH() const {
    return rH_input;
}
const std::vector<double>& ADSB::rD() const {
    return rD_input;
}
const std::vector<double>& ADSB::repAoA() const {
    return repAOA_input;
}
const std::vector<double>& ADSB::azim() const {
    return azim_input;
}
const std::vector<double>& ADSB::rTime() const {
    return time_input;
}