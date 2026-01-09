struct S {
    int x;
};

int g;

int main() {
    struct S s;
    s.x = g;
}
