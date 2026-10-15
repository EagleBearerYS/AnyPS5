#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
#include <string_view>
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

void ClosingJoinsPreparation() {
    using namespace AgcDriver::DriverDetail;
    std::promise<std::thread::id> prepared;
    std::promise<std::thread::id> prebuilt;
    auto preparedOn = prepared.get_future();
    auto prebuiltOn = prebuilt.get_future();
    SubmitRegistrationPreparation([&] { prepared.set_value(std::this_thread::get_id()); });
    SubmitLibraryPrebuild([&] { prebuilt.set_value(std::this_thread::get_id()); });
    const auto caller = std::this_thread::get_id();
    Require(preparedOn.get() != caller && prebuiltOn.get() != caller, "a queued preparation ran on the caller instead of a preparation thread");
    Require(RegistrationPreparationThreads() != 0, "a queued preparation started no preparation thread");
    CloseRegistrationPreparation();
    Require(RegistrationPreparationThreads() == 0, "closing the shader preparation left its threads running");
}

}

int main(int argc, char** argv) {
    try {
        const std::string_view mode = argc > 1 ? argv[1] : "";
        if (mode == "--joined") ClosingJoinsPreparation();
        else ClosingStartsNoPreparation();
        std::cout << "shutdown threads tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
