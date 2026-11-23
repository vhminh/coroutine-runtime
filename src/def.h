#pragma once

#include <memory>

template <class T>
using Box = std::unique_ptr<T>;

template <class T>
using Arc = std::shared_ptr<T>;
