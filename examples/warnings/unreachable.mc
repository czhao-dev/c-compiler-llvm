// Triggers: "unreachable code"
//
// The second `return` can never execute because the first `return`
// always exits the function first.
int main() {
    int x = 0;
    return x;
    return 1;
}
