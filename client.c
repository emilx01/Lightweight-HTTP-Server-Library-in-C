#include <stdio.h>

struct HttpRequest {
    char *method;
    int status;
};

void print_req(struct HttpRequest *self) {
    printf("%s %d\n", self->method, self->status);
}

// Executable code MUST live inside main()
int main(void) {
    struct HttpRequest req = {"GET", 200};
    print_req(&req);
    return 0;
}

