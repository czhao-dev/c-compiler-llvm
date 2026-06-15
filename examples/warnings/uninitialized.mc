// Triggers: "variable 'x' may be used uninitialized"
//
// `x` is declared but never given a value before it's read.
int main() {
    int x;
    int y = x + 1;
    return y;
}
