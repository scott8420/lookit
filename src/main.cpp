#include "App.hpp"

// Thin bootstrap (CANON). Everything real is in App / Shell / the core.
int main(int argc, char** argv) {
    return lookit::App::create()->run(argc, argv);
}
