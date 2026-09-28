#include "../include/commands.h"
#include "../include/utils.h"


int nb_variable = 0;
char** arg;
struct command_tree* tree = NULL;
char history[MAX_HISTORY_SIZE][MAX_COMMAND_SIZE];
int history_pos = 0;
int history_size = 0;
char hist_file[MAX_HISTORY_SIZE + 20];


void add_my_history(char *cmd)
{
    strcpy(history[history_pos++], cmd);
    history_pos = history_pos % MAX_HISTORY_SIZE;

    if (history_size < MAX_HISTORY_SIZE) 
    {
        history_size++;
    }
}



void commands_init()
{
    tree = command_tree_new();
    ASSERT(tree, );

    arg = calloc(16, sizeof(char*));
    ASSERT(arg, );
    for (int i = 0 ; i < 16 ; i++) 
    {
        arg[i] = calloc(MAX_ELEM_SIZE, sizeof(char));
        ASSERT(arg[i], );
    }
}



struct command* command_new(char cmds[MAX_COMMAND_SIZE], int (*cb)(int argc, char** argv))
{
    struct command* cmd = calloc(1, sizeof(struct command));
    cmd->cb = cb;
    strcpy(cmd->command, cmds);

    return cmd;
}



struct command_node* command_node_new(char carac[MAX_ELEM_SIZE], bool is_arg)
{
    struct command_node* cmd_n = calloc(1, sizeof(struct command_node));
    if (!is_arg) 
    {
        strcpy(cmd_n->carac, carac);
    }

    cmd_n->is_variable = is_arg;

    return cmd_n;
}



struct command_tree* command_tree_new()
{
    struct command_tree* cmd_tr = calloc(1, sizeof(struct command_tree));

    char elem[MAX_ELEM_SIZE];
    memset(elem, 0, MAX_ELEM_SIZE);
    cmd_tr->root = command_node_new(elem, False);

    return cmd_tr;
}



void free_command_tree(struct command_tree* t)
{
    if (!t) 
    {
        return;
    }

    free_command_node(t->root);
    free(t);
}



void free_command(struct command* cmd)
{
    if (!cmd) 
    {
        return;
    }

    free(cmd);
}



void free_command_node(struct command_node* node)
{
    if (!node) 
    {
        return;
    }

    struct command_node* tmp, *next;

    for (tmp = node->childs ; tmp ; tmp = next) 
    {
        next = tmp->nextbrother;
        free_command_node(tmp);
    }

    free_command(node->command);
    free(node);
}



void command_finish()
{
    free_command_tree(tree);
    for (int i = 0 ; i < 16 ; i++) 
    {
        free(arg[i]);
    }

    free(arg);
}



int get_first_elem_command(char command[MAX_COMMAND_SIZE], char elem[MAX_ELEM_SIZE])
{
    int i = 0;
    memset(elem, 0, MAX_ELEM_SIZE);
    for ( ; i < MAX_ELEM_SIZE ; i++) 
    {
        if (command[i] == ' ' || command[i] == '\0' || command[i] == '\n') 
        {
            if (command[i] == ' ') 
            {
                i++;
            }
            break;
        }
        elem[i] = command[i];
    }
    return i;
}



void remove_n_chars(char command[MAX_COMMAND_SIZE], char new_command[MAX_COMMAND_SIZE], int nb_char)
{
    int i = nb_char;
    memset(new_command, 0, MAX_COMMAND_SIZE);
    for ( ; i < MAX_COMMAND_SIZE ; i++) 
    {
        new_command[i - nb_char] = command[i];
    }
}



bool is_var(char elem[MAX_ELEM_SIZE])
{
    int i = 0;
    for ( ; i < MAX_ELEM_SIZE ; i++) 
    {
        if (elem[i] == '<') 
        {
            return True;
        }
    }

    return False;
}



bool elem_is_arg(char elem[MAX_ELEM_SIZE])
{
    bool is_arg = False;
    int i = 0;
    int offset = 0;

    for ( ; i < MAX_ELEM_SIZE ; i++) 
    {
        if (is_arg) 
        {
            arg[nb_variable][i - offset] = elem[i];
        }

        if (elem[i] == '<') 
        {
            is_arg = True;
            offset++;
        }

        if (elem[i] == '>') 
        {
            offset++;
        }
    }
    if (is_arg) 
    {
        nb_variable++;
    }

    return is_arg;
}



void test_decompo(char command[MAX_COMMAND_SIZE])
{
    char cmd[MAX_COMMAND_SIZE];
    memset(cmd, 0, MAX_COMMAND_SIZE);
    strcpy(cmd, command);
    int size = strlen(command);
    char new_cmd[MAX_COMMAND_SIZE];
    char elem[MAX_ELEM_SIZE];
    int elem_size;

    while (size > 1) 
    {
        elem_size = get_first_elem_command(cmd, elem);
        elem_is_arg(elem);

        remove_n_chars(cmd, new_cmd, elem_size);
        memset(cmd, 0, MAX_COMMAND_SIZE);
        strcpy(cmd, new_cmd);
        size = strlen(cmd);
    }
}



struct command_node* command_node_get_or_add(struct command_node* curr, 
            char elem[MAX_ELEM_SIZE], bool is_arg)
{
    struct command_node* prev = NULL;
    struct command_node* node = curr->childs;
    for ( ; node ; node = node->nextbrother) 
    {
        if (is_arg)
        {
            if (node->is_variable) 
            {
                return node;
            }
        } 
        else 
        {
            if (strcmp(node->carac, elem) == 0) 
            {
                return node;
            }
        }
        prev = node;
    }

    if (!prev) 
    {
        node = command_node_new(elem, is_arg);
        curr->childs = node;
    } 
    else 
    {
        if (!node) 
        {
            node = command_node_new(elem, is_arg);
            prev->nextbrother = node;
        }
    }
    //printf("%s\n", curr->childs->carac);

    return node;
}



struct command_node* command_node_get_not_arg(struct command_node* curr, 
                char elem[MAX_ELEM_SIZE])
{
    struct command_node* node = curr->childs;
    for ( ; node ; node = node->nextbrother) 
    {
        if (strcmp(node->carac, elem) == 0) 
        {
            return node;
        }
    }

    return node;
}



struct command_node* command_node_get_arg(struct command_node* curr)
{
    struct command_node* node = curr->childs;
    for ( ; node ; node = node->nextbrother) 
    {
        if (node->is_variable) 
        {
            return node;
        }
    }

    return node;
}



void print_node(struct command_node* node, int offset)
{
    for (int i = 0 ; i < offset ; i++)
    {
        printf("\t");
    }

    printf("%s\n", node->carac);
    struct command_node* next = node->childs;
    for ( ; next ; next = next->nextbrother) 
    {
        print_node(next, offset+1);
    }
}



void print_tree(struct command_tree* tree)
{
    print_node(tree->root, 0);
}



int add_command_to_tree(char command[MAX_COMMAND_SIZE], int (*cb)(int argc, char** argv))
{
    char cmd[MAX_COMMAND_SIZE];
    memset(cmd, 0, MAX_COMMAND_SIZE);
    strcpy(cmd, command);
    int size = strlen(command);
    char new_cmd[MAX_COMMAND_SIZE];
    char elem[MAX_ELEM_SIZE];
    int elem_size;
    bool is_arg;
    struct command_node* cmd_node = tree->root;

    while (size) 
    {
        elem_size = get_first_elem_command(cmd, elem);
        is_arg = is_var(elem);
        cmd_node = command_node_get_or_add(cmd_node, elem, is_arg);
        remove_n_chars(cmd, new_cmd, elem_size);
        memset(cmd, 0, MAX_COMMAND_SIZE);
        strncpy(cmd, new_cmd, MAX_COMMAND_SIZE);
        size = strlen(cmd);
    }

    cmd_node->command = command_new(command, cb);

    return EXIT_SUCCESS;
}



int execute_command(char command[MAX_COMMAND_SIZE])
{
    char cmd[MAX_COMMAND_SIZE];
    memset(cmd, 0, MAX_COMMAND_SIZE);
    strcpy(cmd, command);
    char new_cmd[MAX_COMMAND_SIZE];
    char elem[MAX_ELEM_SIZE];
    memset(elem, 0, MAX_ELEM_SIZE);
    int elem_size;
    struct command_node* tmp_node = NULL;
    struct command_node* cmd_node = tree->root;

    /* Get the first element of the tree */
    elem_size = get_first_elem_command(cmd, elem);

    while (elem_size) 
    {
        /* Get the corresponding node */
        tmp_node = command_node_get_not_arg(cmd_node, elem);

        /* Maybe it is a node with an ARG */
        if (!tmp_node) 
        {
            cmd_node = command_node_get_arg(cmd_node);
            if (cmd_node) 
            {
                strcpy(arg[nb_variable], elem);
                nb_variable++;
            }
        } 
        else 
        {
            cmd_node = tmp_node;
        }

        /* If no ,ode found at this point, command not found */
        if (!cmd_node) 
        {
            for (int i = 0 ; i < 16 ; i++) 
            {
                memset(arg[i], 0, MAX_ELEM_SIZE);
            }
            nb_variable = 0;
            return COMMAND_NOT_FOUND;
        }

        /* Update the command as well as the current elem */
        remove_n_chars(cmd, new_cmd, elem_size);
        strcpy(cmd, new_cmd);
        memset(elem, 0, MAX_ELEM_SIZE);
        elem_size = get_first_elem_command(cmd, elem);
    }

    if (!cmd_node->command) 
    {
        for (int i = 0 ; i < 16 ; i++) 
        {
            memset(arg[i], 0, MAX_ELEM_SIZE);
        }
        nb_variable = 0;
        return COMMAND_INCOMPLETE;
    }

    int ret = (*cmd_node->command->cb)(nb_variable, arg);

    
    for (int i = 0 ; i < 16 ; i++) 
    {
        memset(arg[i], 0, MAX_ELEM_SIZE);
    }
    nb_variable = 0;
    return ret;
}



void print_command_result(int commandSock, int ret, char cmd[1024])
{
    char buf[MAX_COMMAND_SIZE];

    switch(ret)
    {
        case COMMAND_NOT_FOUND:
            snprintf(buf, MAX_COMMAND_SIZE, "Command %% %s %% not found", cmd);
            _ERROR("%s", buf);
            if (send_no_sigpipe(commandSock, buf, strlen(buf), 0) == -1)
            {
                perror("Send");
            }
            break;
        
        case COMMAND_INCOMPLETE:
            snprintf(buf, MAX_COMMAND_SIZE, "Command %% %s %% might be incomplete", cmd);
            _ERROR("%s", buf);
            send_no_sigpipe(commandSock, buf, strlen(buf), 0);
            break;

        case COMMAND_FAILED:
            snprintf(buf, MAX_COMMAND_SIZE, "Command %% %s %% failed", cmd);
            _ERROR("%s", buf);
            send_no_sigpipe(commandSock, buf, strlen(buf), 0);
            break;
        
        default:
            break;

    }
    
    memset(buf, 0, MAX_COMMAND_SIZE);
    buf[0] = '$';
    send_no_sigpipe(commandSock, buf, 1, 0);
}
