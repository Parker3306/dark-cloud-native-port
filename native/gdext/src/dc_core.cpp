#include "dc_core.h"
#include <dccore/version.h>

using namespace godot;

void DcCore::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_version"), &DcCore::get_version);
}

String DcCore::get_version() const {
    return String("dccore ") + String(dccore::version());
}
