#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <execinfo.h>

int extract_address_and_subtract(const char *symbol, char *address_buffer, size_t buffer_size) {
    // Find the beginning and end of the address enclosed in square brackets
    const char *begin = strchr(symbol, '[');  // Find '[' character
    const char *end = strchr(symbol, ']');    // Find ']' character

    if (begin && end && begin < end) {  // Ensure both characters are found and '[' comes before ']'
        // Extract the address string
        size_t length = end - (begin + 1);  // Calculate length of address string
        if (length < buffer_size) {
            char temp_address[64];
            strncpy(temp_address, begin + 1, length);  // Copy address to temporary buffer
            temp_address[length] = '\0';  // Null-terminate the extracted address

            // Convert the address from string to unsigned long
            unsigned long address_value = strtoul(temp_address, NULL, 16);

            // Subtract 1 from the address
            address_value -= 1;

            // Convert back to hexadecimal string
            snprintf(address_buffer, buffer_size, "0x%lx", address_value);

            return 1;  // Successful extraction and modification
        }
    }

    return 0;  // Extraction or modification failed
}

int run_command_and_capture_output(const char *command, char *output_buffer, size_t buffer_size) {
    FILE *fp;
    if ((fp = popen(command, "r")) == NULL) {
        perror("popen failed");
        return 0;  // failed to run command
    }

    // Read the output of the command into the buffer
    fgets(output_buffer, buffer_size, fp);

    // Close the file pointer
    pclose(fp);

    return 1;  // Successfully captured output
}

void print_stack_trace(void) {
    void *buffer[10];
    int size = backtrace(buffer, 10);
    char **symbols = backtrace_symbols(buffer, size);

    if (symbols == NULL) {
        perror("backtrace_symbols");
        exit(EXIT_FAILURE);
    }

    char exec_name[256];
    sscanf(symbols[0], "%255[^()]s", exec_name);  // Extract executable name from first symbol
    // i = 1 to skip print_stack_trace
    for (int i = 1; i < size; i++) {
        char address[64];
        if (extract_address_and_subtract(symbols[i], address, sizeof(address))) {
            // Call addr2line using the modified address and executable name
            char command[2048];
            snprintf(command, sizeof(command), "addr2line -f -p -e %s %s", exec_name, address);

            // Buffer to capture the output of addr2line
            char command_output[4096];
            if (run_command_and_capture_output(command, command_output, sizeof(command_output))) {
                printf("%s", command_output); 
            } else {
                printf("failed to capture output for command: %s\n", command);
            }
        } else {
            printf("failed to extract or modify address from: %s\n", symbols[i]);
        }
    }

    free(symbols);  // Free memory allocated for symbols
}

void function2() {
    print_stack_trace();
}

void function1() {
    function2();
}

int main() {
    function1();
    return 0;
}
