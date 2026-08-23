#pragma once

#include "godot/ref_counted.hpp"

namespace netw {

class NetwDisplayRoleFacts : public godot::RefCounted {
    GDCLASS(NetwDisplayRoleFacts, godot::RefCounted)

private:
    bool simulates_locally = false;
    bool controlled_locally = false;
    bool predicted_input = false;
    bool prediction_registered = false;
    bool authors_streams = false;
    bool owner_is_authority = false;

protected:
    static void _bind_methods();

public:
    int resolve() const;

    void set_simulates_locally(bool value) { simulates_locally = value; }
    bool get_simulates_locally() const { return simulates_locally; }
    void set_controlled_locally(bool value) { controlled_locally = value; }
    bool get_controlled_locally() const { return controlled_locally; }
    void set_predicted_input(bool value) { predicted_input = value; }
    bool get_predicted_input() const { return predicted_input; }
    void set_prediction_registered(bool value) {
        prediction_registered = value;
    }
    bool get_prediction_registered() const { return prediction_registered; }
    void set_authors_streams(bool value) { authors_streams = value; }
    bool get_authors_streams() const { return authors_streams; }
    void set_owner_is_authority(bool value) { owner_is_authority = value; }
    bool get_owner_is_authority() const { return owner_is_authority; }
};

} // namespace netw
