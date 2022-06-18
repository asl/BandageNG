add_executable(BandageTests bandagetests.cpp)
add_test(NAME BandageTests COMMAND BandageTests)

target_link_libraries(BandageTests PRIVATE BandageLib OGDF Qt6::Widgets Qt6::Test)
