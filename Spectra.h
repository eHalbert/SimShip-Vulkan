/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include <cmath>
#include <vector>
#include <algorithm>

// 2. LIB
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
using namespace glm;

// 3. WIN
#define _USE_MATH_DEFINES
#include <math.h>
#include <iostream>
#include <iomanip>
using namespace std;


// Sea in development pending fetch and wind speed: JONSWAP
class JONSWAPModel 
{
private:
    static constexpr double g = 9.81; // Acceleration due to gravity (m/s^2)

public:
    struct WaveParameters
    {
        double significantWaveHeight; // Hs in meters
        double peakPeriod;            // Tp in seconds
        double peakFrequency;         // fp in Hz
        double peakWavelength;        // p in meters
    };

    static WaveParameters GetWaveParameters(double windSpeed, double fetch)
    {
        double U10 = windSpeed;

        // Dimensionless fetch parameter
        double chi = g * fetch / (U10 * U10);

		// -- Significant wave height --------------------------------------------------
        // JONSWAP formula (Shore Protection Manual / Hasselmann 1973)
        double Hs = 0.0016 * sqrt(chi) * U10 * U10 / g;

        // -- Peak period ------------------------------------------------
        // Standard JONSWAP formula
        double Tp = 0.2857 * pow(chi, 1.0 / 3.0) * U10 / g;

        // -- Peak frequency ----------------------------------------------
        double fp = 1.0 / Tp;

        // -- Peak wavelength (deep water) ---------------------------
        //  = g·Tp² / (2·)
        double lambda_p = g * Tp * Tp / (2.0 * M_PI);

        // -- Verification of physical limits ----------------------------
        // Fully developed sea: chi > 2.37e4 (Pierson-Moskowitz)
        bool fullyDeveloped = (chi > 23700.0);

        if (fullyDeveloped)
        {
            // Fully developed sea: Pierson-Moskowitz
            Hs = 0.2112 * U10 * U10 / g;
            Tp = 0.7303 * U10 / g * 2.0 * M_PI;  // Tp = 2·U10 / (0.879·g)
        }

        return { Hs, Tp, fp, lambda_p };
    }
};

// The Phillips spectrum models the energy of wind-generated waves with a theoretical shape, often expressed in terms of wavenumber k or frequency f.
// The Phillips constant alpha(Ph) is typically around 0.0081.
class PhillipsModel
{
private:
    static constexpr double g = 9.81;

public:
    struct WaveParameters
    {
        double significantWaveHeight; // Hs in meters
        double peakPeriod;            // Tp in seconds
        double peakFrequency;         // fp in Hz
        double peakWavelength;        // p in meters
        double L;                     // characteristic wavelength V²/g
    };

    static WaveParameters GetWaveParameters(double windSpeed)
    {
        double U10 = windSpeed;

        // -- Characteristic wavelength ------------------------------
        // L = U10² / g : largest possible wave for this wind
        double Lc = U10 * U10 / g;

        // -- Significant wave height ----------------------------------------
        // Phillips does not provide an analytical Hs directly
        // We use the empirical relation derived from the integration of the spectrum:
        // Hs  0.0248 · U10² / g  (equivalent =0.0081 integrated over k)
        double Hs = 0.0248 * U10 * U10 / g;

        // -- Peak frequency ----------------------------------------------
        // p = g / U10  (frequency where the Phillips spectrum is maximal)
        // fp = p / 2
        double omega_p = g / U10;
        double Tp = 2.0 * M_PI / omega_p;   // = 2·U10 / g
        double fp = 1.0 / Tp;

        // -- Peak wavelength (deep water) --------------------------
        // p = g·Tp² / (2)
        double lambda_p = g * Tp * Tp / (2.0 * M_PI);

        return { Hs, Tp, fp, lambda_p, Lc };
    }
};

// Fully developed sea
class PiersonMoskowitzModel
{
private:
    static constexpr double g = 9.81;
    static constexpr double PI2 = 2.0 * M_PI;

public:
    struct WaveParameters
    {
        double significantWaveHeight; // Hs in meters
        double peakPeriod;            // Tp in seconds
        double peakFrequency;         // fp in Hz
        double peakWavelength;        // p in meters
        double peakAngularFrequency;  // p in rad/s
    };

    static WaveParameters GetWaveParameters(double windSpeed)
    {
        double U10 = windSpeed;

        // -- Peak angular frequency ------------------------------------
        // p = 0.879 · g / U10  (Pierson-Moskowitz 1964)
        double omega_p = 0.879 * g / U10;

        // -- Peak period ------------------------------------------------
        double Tp = PI2 / omega_p;             // = 2·U10 / (0.879·g)

        // -- Significant wave height ----------------------------------------
        // Analytical integration of the P-M spectrum:
        // Hs = 4·sqrt(m0) = 0.2112 · U10² / g
        double Hs = 0.2112 * U10 * U10 / g;

        // -- Peak frequency ----------------------------------------------
        double fp = 1.0 / Tp;

        // -- Peak wavelength (deep water) --------------------------
        // p = g·Tp² / (2)
        double lambda_p = g * Tp * Tp / PI2;

        return { Hs, Tp, fp, lambda_p, omega_p };
    }
};

// Spectrum used in shallow water
class TMA_Model
{
private:
    static constexpr double g = 9.81;
    static constexpr double PI2 = 2.0 * M_PI;

public:
    struct WaveParameters
    {
        double significantWaveHeight; // Hs in meters
        double peakPeriod;            // Tp in seconds
        double peakFrequency;         // fp in Hz
        double peakWavelength;        // p in meters
        double peakAngularFrequency;  // p in rad/s
        double phi;                   // TMA transformation factor [0,1]
        bool   shallowWater;          // true if shallow water
    };

    static WaveParameters GetWaveParameters(double windSpeed, double fetch, double waterDepth)
    {
        double U10 = windSpeed;
        double h = glm::max(waterDepth, 0.1);  // avoid division by zero

        // -- Step 1: Base JONSWAP -------------------------------------
        // TMA = JONSWAP · (kh), so we start with JONSWAP parameters
        double chi = g * fetch / (U10 * U10);  // dimensionless fetch
        // JONSWAP peak frequency
        double omega_p = 22.0 * pow(g * g / (U10 * fetch), 1.0 / 3.0);
        double Tp_j = PI2 / omega_p;

        // JONSWAP significant wave height
        double Hs_j = 0.0016 * sqrt(chi) * U10 * U10 / g;

        // -- Step 2: TMA transformation function (kh) ---------------
        // kp = p² / g  (deep water wavenumber at the peak)
        double k_p = omega_p * omega_p / g;

        // Shallow water dispersion relation to find the real k_p:
        // omega² = g·k·tanh(k·h)  solved iteratively using Newton's method
        double k_shallow = k_p;  // initialisation deep water
        for (int i = 0; i < 20; i++)
        {
            double kh = k_shallow * h;
            double tanh_kh = tanh(kh);
            double f = k_shallow * tanh_kh - omega_p * omega_p / g;
            double df = tanh_kh + kh * (1.0 - tanh_kh * tanh_kh);
            k_shallow -= f / df;  // Newton iteration
        }

        // Deep and shallow water group velocity at the peak
        double kh_p = k_shallow * h;
        double tanh_kh_p = tanh(kh_p);
        double cg_deep = g / (2.0 * omega_p);
        double cg_shallow = (g / (2.0 * omega_p))
            * (tanh_kh_p + kh_p * (1.0 - tanh_kh_p * tanh_kh_p));

        //  = cg_shallow / cg_deep  [0, 1]
        double phi = cg_shallow / cg_deep;
        phi = glm::clamp(phi, 0.0, 1.0);

        // -- Step 3: Apply TMA transformation to Hs ----------------------------
        // S_TMA(k) = S_JONSWAP(k) · (kh)
        // Integrated over k: Hs_TMA  Hs_JONSWAP · sqrt()
        // because Hs = 4·sqrt(S dk) and  is a multiplicative factor
        double Hs_tma = Hs_j * sqrt(phi);

        // -- Step 4: Correct Tp in shallow water ----------------
        // In shallow water, the group velocity decreases, apparent period increases   
        // Tp_TMA  Tp_JONSWAP / sqrt(tanh(kp·h))
        double Tp_tma = Tp_j / sqrt(tanh_kh_p);

        // -- Derivatives -------------------------------------------------------
        double fp = 1.0 / Tp_tma;
        double lambda_p = PI2 / k_shallow;  // real wavelength in shallow water

        // -- Shallow water detection ------------------------------------
        // kh < /10  shallow water, kh >   deep water
        bool shallowWater = (kh_p < M_PI / 2.0);

        return { Hs_tma, Tp_tma, fp, lambda_p, omega_p, phi, shallowWater };
    }
};

// Initial Pierson-Moskowitz model + alpha parameterization
class HasselmannModel
{
private:
    static constexpr double g = 9.81;
    static constexpr double PI2 = 2.0 * M_PI;

public:
    struct WaveParameters
    {
        double significantWaveHeight; // Hs in meters
        double peakPeriod;            // Tp in seconds
        double peakFrequency;         // fp in Hz
        double peakWavelength;        // p in meters
        double peakAngularFrequency;  // p in rad/s
        double alpha;                 // Phillips coefficient 
        double gamma;                 // peak factor  (JONSWAP)
        double epsilon;               // dimensionless fetch
        bool   fullyDeveloped;        // fully developed sea
    };

    static WaveParameters GetWaveParameters(double windSpeed, double fetch,
        double gamma = 3.3)
    {
        double U10 = windSpeed;

        // -- Dimensionless fetch ------------------------------------------
        //  = g·F / U10²  (Hasselmann 1973)
        double epsilon = g * fetch / (U10 * U10);

        // -- Phillips coefficient (Hasselmann 1973, eq. 3.5) ----------
        //  = 0.0662 · ^(-0.22)
        //  decreases with fetch — mature sea has less high-frequency energy
        double alpha_h = 0.0662 * pow(epsilon, -0.22);
        alpha_h = std::clamp(alpha_h, 0.0060, 0.0200);

        // -- Peak frequency (Hasselmann 1973, eq. 3.6) -------------------
        // p = 22 · (g² / U10·F)^(1/3)
        // Identical to JONSWAP — established by Hasselmann
        double omega_p = 22.0 * pow(g * g / (U10 * fetch), 1.0 / 3.0);
        double Tp = PI2 / omega_p;

        // -- Significant wave height (Hasselmann 1973, eq. 3.7) -------------
        // Hs = 0.0016 · sqrt() · U10² / g
        // Identical to standard JONSWAP
        double Hs = 0.0016 * sqrt(epsilon) * U10 * U10 / g;

        // -- Parameter  (peak width) ----------------------------------
        // Hasselmann defines () variable according to /p
        //  = 0.07 if   p,  = 0.09 if  > p
        // (implemented in the spectrum, not in the integral parameters)

        // -- Parameter  (peak factor) ---------------------------------
        //  = 3.3 average value measured in the North Sea (JONSWAP 1973)
        // Hasselmann showed that  varies between 1 and 7 depending on conditions
        // Exposed as a parameter to allow adjustment
        double gamma_h = std::clamp(gamma, 1.0, 7.0);

        // -- Correction of Hs by peak factor ----------------------------------------
        // The resonance peak increases the total energy of the spectrum
        // Empirical correction: Hs_ = Hs · sqrt(1 + 0.13·(gamma - 1))
        double Hs_corrected = Hs * sqrt(1.0 + 0.13 * (gamma_h - 1.0));

        // -- Derivatives -------------------------------------------------------
        double fp = 1.0 / Tp;
        double lambda_p = g * Tp * Tp / PI2;

        // -- Fully developed sea check ----------------------------------------
        //  > 2.37e4  Pierson-Moskowitz (Hasselmann 1973)
        bool fullyDeveloped = (epsilon > 23700.0);
        if (fullyDeveloped)
        {
            Hs_corrected = 0.2112 * U10 * U10 / g;
            Tp = PI2 * U10 / (0.879 * g);
            omega_p = PI2 / Tp;
            alpha_h = 0.0081;  // deep water Phillips constant
        }

        return { Hs_corrected, Tp, fp, lambda_p, omega_p, alpha_h, gamma_h,
                 epsilon, fullyDeveloped };
    }
};

// The Donelan and Banner model [1985,[1996] proposes a spectral description based on two regimes, with an adjustment for the spectral slope, complex to calculate integrally.
// The significant wave height and peak period can be approximated by empirical formulas based on U10 and fetch.
class DonelanBannerModel
{
private:
    static constexpr double g = 9.81;
    static constexpr double PI2 = 2.0 * M_PI;

public:
    struct WaveParameters
    {
        double significantWaveHeight; // Hs in meters
        double peakPeriod;            // Tp in seconds
        double peakFrequency;         // fp in Hz
        double peakWavelength;        // p in meters
        double peakAngularFrequency;  // p in rad/s
        double beta_peak;             // at the peak (DB directional parameter)
    };

    static WaveParameters GetWaveParameters(double windSpeed, double fetch)
    {
        double U10 = windSpeed;

        // -- Dimensionless fetch parameter --------------------------------
        double chi = g * fetch / (U10 * U10);

        // -- Peak frequency ----------------------------------------------
        // Identical to JONSWAP — Donelan-Banner does not modify p
        double omega_p = 22.0 * pow(g * g / (U10 * fetch), 1.0 / 3.0);
        double Tp = PI2 / omega_p;

        // -- Significant wave height -----------------------------------------
        // Identical to JONSWAP — Donelan-Banner does not modify Hs
        // The difference is only in the directional distribution
        double Hs = 0.0016 * sqrt(chi) * U10 * U10 / g;

        // -- Derivatives -------------------------------------------------------
        double fp = 1.0 / Tp;
        double lambda_p = g * Tp * Tp / PI2;

        // -- Directional parameter at the peak -------------------------------
        // This is THE specific contribution of Donelan-Banner
        //  controls the width of the sech²(·) distribution
        // At the peak ( = p, ratio = 1.0) :  = 2.28 * 1^(-1.3) = 2.28
        //  increases for  < p (wider distribution below the peak)
        //  decreases for  > p (wider distribution above the peak)
        double beta_peak = 2.28;  // exact value at the peak (ratio = 1.0)

        // -- Fully developed sea check ----------------------------------------
        // chi > 2.37e4  saturated sea, cap at Pierson-Moskowitz
        if (chi > 23700.0)
        {
            Hs = 0.2112 * U10 * U10 / g;
            Tp = PI2 * U10 / (0.879 * g);
        }

        return { Hs, Tp, fp, lambda_p, omega_p, beta_peak };
    }
};

// The Elfouhaily spectrum is a widely used unified directional model that describes both gravity and capillary waves.
class ElfouhailyModel
{
private:
    static constexpr double g = 9.81;
    static constexpr double PI2 = 2.0 * M_PI;
    static constexpr double sigma_t = 0.074;   // surface tension N/m
    static constexpr double rho = 1025.0;  // seawater density kg/m³

public:
    struct WaveParameters
    {
        double significantWaveHeight; // Hs in meters
        double peakPeriod;            // Tp in seconds
        double peakFrequency;         // fp in Hz
        double peakWavelength;        // p in meters
        double peakAngularFrequency;  // p in rad/s
        double alpha;                 // Elfouhaily coefficient (variable)
        double beta;                  // Capillary coefficient
        double k_m;                   // minimum capillary wavenumber (rad/m)
        double Omega;                 // wave age U10/cp
    };

    static WaveParameters GetWaveParameters(double windSpeed, double fetch)
    {
        double U10 = windSpeed;

        // -- Dimensionless fetch parameter --------------------------------
        double chi = g * fetch / (U10 * U10);

        // -- Peak frequency ----------------------------------------------
        // Identical to JONSWAP — Elfouhaily does not modify p
        double omega_p = 22.0 * pow(g * g / (U10 * fetch), 1.0 / 3.0);
        double Tp = PI2 / omega_p;
        double k_p = omega_p * omega_p / g;  // peak wavenumber

        // -- Phase velocity at the peak -----------------------------------
        // Elfouhaily includes surface tension in the phase velocity
        // c(k) = sqrt(g/k + sigma_t·k/rho) 
        double c_p = sqrt(g / k_p + sigma_t * k_p / rho);

        // -- Wave age -----------------------------------------------
        //  = U10 / cp  (1 = mature sea, >1 = young sea, <1 = swell)
        double Omega = U10 / c_p;
        Omega = std::clamp(Omega, 0.84, 5.0);

        // -- Significant wave height -----------------------------------------
        // Identical to JONSWAP — Elfouhaily does not modify Hs
        double Hs = 0.0016 * sqrt(chi) * U10 * U10 / g;

        // -- Variable coefficient (specific to Elfouhaily) ------------------
        //  = 0.006 · sqrt()  — larger for young sea    
        double alpha_e = 0.006 * sqrt(Omega);
        alpha_e = std::clamp(alpha_e, 0.0028, 0.015);

        // -- Capillary coefficient (specific to Elfouhaily) -----------
        //  = 0.229 · exp[-0.4·(/c - 1)²]  with c = 0.84
        const double Omega_c = 0.84;
        double beta_e = 0.229 * exp(-0.4 * pow(Omega / Omega_c - 1.0, 2.0));

        // -- Minimum capillary wavenumber ------------------------------
        // k_m = sqrt(rho * g / sigma_t)  363 rad/m
        // Transition gravity-capillary
        double k_m = sqrt(rho * g / sigma_t);

        // -- Derivatives -------------------------------------------------------
        double fp = 1.0 / Tp;
        double lambda_p = g * Tp * Tp / PI2;

        // -- Fully developed sea check ------------------------
        if (chi > 23700.0)
        {
            Hs = 0.2112 * U10 * U10 / g;
            Tp = PI2 * U10 / (0.879 * g);
        }

        return { Hs, Tp, fp, lambda_p, omega_p, alpha_e, beta_e, k_m, Omega };
    }
}; 
void DisplayWaveParametersFromModels(double windSpeed, double fetch, double waterDepth = 100.0);
