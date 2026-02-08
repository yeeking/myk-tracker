#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>


struct input_event ev[64];
        
char getChar(int file_ref)
    {
        
        int rd, code_id, ev_count, key_code;
        ev_count = 0;
        key_code = 0;
        // keep reading until 
        // we get a key down or key held event
        while(1)
        {
            rd = read(file_ref, ev, sizeof(struct input_event) * 64);
            printf("%i  %i %i \n", ev->code, ev->type, ev->value);
            //printf("read %i\n", rd);
            if (rd > 48) 
            {
              printf("Ignoring\n");
              continue; // holding lots of keys, ignore.
            }
            ev_count = rd / sizeof(struct input_event);
            if(ev_count == 2)
            {
                code_id = 0;
            }
            else if (ev_count == 3)
            {
                code_id = 1;
            }
            //printf("\ncode id: %i", code_id);

            // .code tells you the key they pressed
            key_code = ev[code_id].code;
            // .value tells you key up, down, hold 
            //1 and 2 are key down or held events
            // if (ev[code_id].value == 1 || ev[code_id].value == 2)
            //printf("KeyUtisl:: %i %i \n", key_code, ev[code_id].value);  

            
            if (ev[code_id].value == 0) // only break on key up...
            {
        //          std::cout << "KeyUtils:: breaking " << std::endl;
                break;
            }
            // else {
            //     std::cout << "KeyUtils:: waiting " << std::endl;
            // }
        }
        //std::cout << "KeyUtils:: returning " << key_code << std::endl;

        return key_code;  
    }

int main()
{
    int file_ref = open("/dev/input/by-id/usb-Griffin_Technology__Inc._Griffin_PowerMate-event-if00", O_RDONLY);
    while(1)
        printf("%i", getChar(file_ref));
    return 0;
}