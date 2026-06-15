// Triggers: "control may reach the end of non-void function 'max' without returning a value"
//
// If `a <= b`, control falls off the end of the function without
// executing a `return`.
int max(int a, int b) {
    if (a > b) {
        return a;
    }
}
