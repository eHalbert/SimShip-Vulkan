#include "Spectra.h"

void DisplayWaveParametersFromModels(double windSpeed, double fetch, double waterDepth)
{
    cout << "Significant wave heights (1/3)" << endl;
    cout << "Wind : " << windSpeed << " m/s - Fetch : " << fetch / 1000.0 << " km - Depth : " << waterDepth << " m" << endl;
    cout << "==============================" << endl;
    cout << fixed << setprecision(2);

    auto p0 = PhillipsModel::GetWaveParameters(windSpeed);
    cout << "Phillips :        \t" << p0.significantWaveHeight << " m - Period : " << p0.peakPeriod << " s" << endl;

    auto p1 = PiersonMoskowitzModel::GetWaveParameters(windSpeed);
    cout << "PiersonMoskowitz :\t" << p1.significantWaveHeight << " m - Period : " << p1.peakPeriod << " s" << endl;

    auto p2 = JONSWAPModel::GetWaveParameters(windSpeed, fetch);
    cout << "JONSWAP :         \t" << p2.significantWaveHeight << " m - Period : " << p2.peakPeriod << " s" << endl;

    auto p3 = HasselmannModel::GetWaveParameters(windSpeed, fetch);
    cout << "Hasselmann :      \t" << p3.significantWaveHeight << " m - Period : " << p3.peakPeriod << " s" << endl;

    auto p4 = TMA_Model::GetWaveParameters(windSpeed, fetch, waterDepth);
    cout << "TMA :             \t" << p4.significantWaveHeight << " m - Period : " << p4.peakPeriod << " s" << endl;

    auto p5 = DonelanBannerModel::GetWaveParameters(windSpeed, fetch);
    cout << "DonelanBanner :   \t" << p5.significantWaveHeight << " m - Period : " << p5.peakPeriod << " s" << endl;

    auto p6 = ElfouhailyModel::GetWaveParameters(windSpeed, fetch);
    cout << "Elfouhaily :      \t" << p6.significantWaveHeight << " m - Period : " << p6.peakPeriod << " s" << endl;

    cout << "==============================" << endl;

    // Sanity check : JONSWAP, Hasselmann, DonelanBanner et Elfouhaily must give very close Hs for the same parameters
    // Phillips should be ~10x lower
    // PiersonMoskowitz should be the highest (fully developed sea)
    // TMA should be ≤ JONSWAP (shallow water attenuation)
}