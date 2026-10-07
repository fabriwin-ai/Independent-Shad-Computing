#pragma once

// Umbrella header for Independent-Shad-Computing.

#include "isc/backend/backend.hpp"
#include "isc/core/device_caps.hpp"
#include "isc/core/frame.hpp"
#include "isc/core/profile.hpp"
#include "isc/core/render_target.hpp"
#include "isc/core/resource_cache.hpp"
#include "isc/core/types.hpp"
#include "isc/ir/ir.hpp"
#include "isc/lang/ast.hpp"
#include "isc/lang/lexer.hpp"
#include "isc/lang/parser.hpp"
#include "isc/lod/adaptive_controller.hpp"
#include "isc/opt/optimizer.hpp"
#include "isc/plan/execution_plan.hpp"
#include "isc/runtime.hpp"
#include "isc/sched/scheduler.hpp"
#include "isc/sched/task_graph.hpp"
#include "isc/util/image_io.hpp"
