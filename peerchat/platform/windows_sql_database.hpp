#pragma once
#include "storage_backend.hpp"
#include <string>
namespace peerchat::platform {
// Windows path/connection execution only; schema stays with each native owner.
std::shared_ptr<SqlDatabase> openWindowsSqlDatabase(const std::wstring& directory);
}
