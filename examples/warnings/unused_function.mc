// Triggers: "function 'helper' is defined but never called"
//
// `helper` is never reached from `main` (or anywhere else).
int helper(int x) {
    return x * 2;
}

int main() {
    return 0;
}
