add_executable(BandageTests bandagetests.cpp)
add_test(NAME BandageTests COMMAND BandageTests)

target_link_libraries(BandageTests PRIVATE BandageCLI BandageLib OGDF Qt6::Widgets Qt6::Test CLI11::CLI11)
