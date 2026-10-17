#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Engine-independent rules shared by gameplay and the executable migration tests.
namespace EphemeraSim
{
constexpr double Tau = 6.283185307179586476925286766559;
struct Vec
{
    double x = 0, y = 0, z = 0;
    Vec operator+(Vec b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vec operator-(Vec b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vec operator*(double s) const { return {x*s, y*s, z*s}; }
    double Length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec Cross(Vec b) const { return {y*b.z-z*b.y, z*b.x-x*b.z, x*b.y-y*b.x}; }
};
struct Orbit
{
    double radius=0, period=600, phase=0, eccentricity=0, tilt=0, periAngle=0, periTime=0;
    Vec Position(double t) const
    {
        if (radius <= 0) return {};
        const double e = std::clamp(eccentricity, 0.0, 0.999);
        double r = radius, angle = phase + Tau*t/std::max(period, 0.001);
        if (e > 0)
        {
            const double m = std::remainder(Tau*(t-periTime)/std::max(period, 0.001), Tau);
            double anomaly = e > 0.8 ? (m < 0 ? -Tau/2 : Tau/2) : m;
            for (int i=0; i<16; ++i)
                anomaly -= (anomaly-e*std::sin(anomaly)-m)/(1-e*std::cos(anomaly));
            angle = 2*std::atan2(std::sqrt(1+e)*std::sin(anomaly/2),
                                std::sqrt(1-e)*std::cos(anomaly/2)) + periAngle;
            r *= 1-e*std::cos(anomaly);
        }
        const double z = -std::sin(angle)*r;
        return {std::cos(angle)*r, -z*std::sin(tilt), z*std::cos(tilt)};
    }
};
struct DilationPoint { double radius, factor; };
inline double Dilation(double radius, const std::vector<DilationPoint>& depths)
{
    if (depths.empty()) return 1;
    auto previous = depths.front();
    if (radius >= previous.radius)
    {
        const double alpha = std::clamp((radius-previous.radius)/(previous.radius*0.6), 0.0, 1.0);
        return previous.factor + (1-previous.factor)*alpha;
    }
    for (size_t i=1; i<depths.size(); ++i)
    {
        const auto current = depths[i];
        if (radius >= current.radius)
        {
            const double alpha = (radius-current.radius)/(previous.radius-current.radius);
            return current.factor + (previous.factor-current.factor)*alpha;
        }
        previous = current;
    }
    return previous.factor;
}
inline Vec Gravity(Vec offsetToCenter, double radius, double surface, double maximum)
{
    const double distance = offsetToCenter.Length();
    if (distance < 0.001 || radius <= 0) return {};
    const double g = distance >= radius ? surface*radius*radius/(distance*distance) : surface*distance/radius;
    return offsetToCenter * (std::min(g, maximum)/distance);
}
inline Vec ChangeFrameVelocity(Vec relative, Vec oldPointVelocity, Vec newPointVelocity)
{
    return relative + oldPointVelocity - newPointVelocity;
}
struct Clocks
{
    double world=0, proper=0, dilation=1;
    void Advance(double dt, double targetDilation)
    {
        dilation += (std::max(1.0, targetDilation)-dilation)*std::clamp(dt*2.5, 0.0, 1.0);
        proper += dt;
        world += dt*dilation;
    }
    void Reset() { world=proper=0; dilation=1; }
};
inline double Resonance(double t)
{
    double result=0;
    for (const double peak : {420.0, 1080.0})
    {
        const double x = std::clamp((std::abs(t-peak)-5)/16, 0.0, 1.0);
        result = std::max(result, 1-x*x*(3-2*x));
    }
    return result;
}
}
