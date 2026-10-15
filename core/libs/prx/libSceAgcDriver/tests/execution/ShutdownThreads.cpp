#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void ClosingStartsNoPreparation() {
    using namespace AgcDriver::DriverDetail;
    Require(RegistrationPreparationThreads() == 0, "a shader preparation thread ran before anything was prepared");
    CloseRegistrationPreparation();
    Require(RegistrationPreparationThreads() == 0, "closing the shader preparation started preparation threads");
    const auto caller = std::this_thread::get_id();
    bool ranHere = false;
    SubmitRegistrationPreparation([&] { ranHere = std::this_thread::get_id() == caller; });
    SubmitLibraryPrebuild([&] { ranHere = ranHere && std::this_thread::get_id() == caller; });
    Require(ranHere, "a preparation submitted after closing ran on another thread");
    Require(RegistrationPreparationThreads() == 0, "a preparation submitted after closing started preparation threads");
}

}

int main() {
    try {
        ClosingStartsNoPreparation();
        std::cout << "shutdown threads tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
