#pragma once
#include "dm_attachment_descriptor.hpp"
#include "group_attachment_finalizer.hpp"
namespace peerchat::dm_attachments { using Finalizer=group_attachments::BlockFinalizer<true>; }
