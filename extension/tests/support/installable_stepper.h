#pragma once

#include "netw/api/physics_stepper.hpp"

namespace netw_test {

class InstallableStepper : public netw::NetwPhysicsStepper {
public:
    bool can_step() override {
        return true;
    }
};

} // namespace netw_test
