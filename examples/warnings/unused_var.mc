// Triggers: "variable 'temp' is declared but never used"
//
// `temp` receives the result of compute() but is never read afterwards.
int compute() {
    return 42;
}

int main() {
    int temp = compute();
    return 0;
}
