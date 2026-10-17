#include "SimulationMath.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
using namespace EphemeraSim;
bool Close(double a,double b,double epsilon=1e-8){return std::abs(a-b)<epsilon;}
void SelfTest()
{
    Orbit circle{1300,6000,0.4,0,0.015};
    assert(Close((circle.Position(0)-circle.Position(6000)).Length(),0,1e-8));
    Orbit ellipse{1700,2000,0,0.72,0,0,350};
    assert(Close(ellipse.Position(350).Length(),476));
    assert(Close(ellipse.Position(1350).Length(),2924));
    Orbit steep{1700,2000,0,0.97,0,0,350};
    assert(Close(steep.Position(350).Length(),51));
    assert(Close(steep.Position(1350).Length(),3349));
    const std::vector<DilationPoint> depths{{175,1.5},{135,2},{105,6},{70,12},{52,12},{30,400}};
    assert(Close(Dilation(280,depths),1));
    assert(Close(Dilation(175,depths),1.5));
    assert(Close(Dilation(120,depths),4));
    assert(Close(Dilation(70,depths),12));
    assert(Close(Dilation(20,depths),400));
    assert(Close(Gravity({-100,0,0},100,10,40).x,-10));
    assert(Close(Gravity({-200,0,0},100,10,40).x,-2.5));
    assert(Close(Gravity({-50,0,0},100,10,40).x,-5));
    assert(Close(Gravity({},100,10,40).Length(),0));
    Vec relative{2,0,0},oldFrame{25,4,0},newFrame{-12,0,3};
    const auto changed=ChangeFrameVelocity(relative,oldFrame,newFrame);
    assert(Close((changed+newFrame-(relative+oldFrame)).Length(),0));
    Clocks clock;
    for(int i=0;i<900;++i)clock.Advance(1.0/90,12);
    assert(Close(clock.proper,10,1e-10));
    assert(clock.world>115&&clock.world<120);
    clock.Reset();assert(clock.world==0&&clock.proper==0&&clock.dilation==1);
    assert(Close(Resonance(420),1));assert(Close(Resonance(1080),1));
    assert(Close(Resonance(300),0));assert(Close(Resonance(441),0));
}
int main(int argc,char**)
{
    SelfTest();
    if(argc==1){std::cout<<"Simulation invariants passed.\n";return 0;}
    // A small stdin protocol lets Python compare native rules to actual Godot snapshots.
    std::string command;
    std::cout<<std::setprecision(17);
    while(std::cin>>command)
    {
        if(command=="orbit")
        {
            Orbit o;double t;
            std::cin>>o.radius>>o.period>>o.phase>>o.eccentricity>>o.tilt>>o.periAngle>>o.periTime>>t;
            auto p=o.Position(t);std::cout<<p.x<<" "<<p.y<<" "<<p.z<<"\n";
        }
    }
}
