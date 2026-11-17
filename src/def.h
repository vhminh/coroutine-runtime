#pragma once

#include <memory>

template <class T>
using Box = std::unique_ptr<T>;
