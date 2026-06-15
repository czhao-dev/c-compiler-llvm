// Triggers: "division by zero"
//
// Constant propagation determines the divisor is always the literal 0.
int main() {
    int total = 10;
    int bad = total / 0;
    return bad;
}
