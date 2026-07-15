# Thin pybind11 wrapper around lob_mm's SimulationRunner (PROJECT_SPEC.md
# §11) -- no logic of its own beyond type/function bindings; Python drives
# sweeps and plotting, every strategy stays C++-only.
pybind11_add_module(lob_bindings bindings.cpp)
target_link_libraries(lob_bindings PRIVATE lob_mm)
