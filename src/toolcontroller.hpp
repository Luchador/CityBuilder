#pragma once

#include <memory>

#include "tool.hpp"

class ToolController
{
    public:
        std::unique_ptr<Tool> currentTool;

};