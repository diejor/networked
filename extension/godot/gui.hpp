#pragma once

#if defined(NETW_MODULE)
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tree.h"

namespace godot {
using ::CheckBox;
using ::Control;
using ::HBoxContainer;
using ::Label;
using ::LineEdit;
using ::OptionButton;
using ::SpinBox;
using ::Tree;
using ::TreeItem;
using ::VBoxContainer;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/box_container.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif
