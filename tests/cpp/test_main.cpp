#include <iostream>

int RunPointFilterTests();
int RunStrataStatBundleTests();

int main() {
    int failures = 0;
    failures += RunPointFilterTests();
    failures += RunStrataStatBundleTests();

    if (failures == 0) {
        std::cout << "\nAll fusion_tests passed.\n";
    } else {
        std::cout << "\n" << failures << " fusion_tests failure(s).\n";
    }
    return failures == 0 ? 0 : 1;
}
