// yui Showcase v2
// Single include for the showcase application
//
// Architecture:
//   Theme.hpp      - Theme struct and presets (colors, radii, spacing)
//   State.hpp      - AppState and reactive store access
//   Primitives.hpp - Theme-aware UI building blocks (Button, Card, Input, etc.)
//   Features.hpp   - Feature sections composed from primitives
//   App.hpp        - Main application layout
//
// Usage:
//   #include "showcase/showcase.hpp"
//
//   // In main:
//   yui::Store<showcase::AppState> store;
//   showcase::storePtr = &store;
//
//   auto ui = showcase::App("Title", "SDL2");
//
//   // Cleanup before exit:
//   showcase::storePtr = nullptr;

#pragma once

#include "App.hpp"
#include "Features.hpp"
#include "Primitives.hpp"
#include "State.hpp"
#include "Theme.hpp"
