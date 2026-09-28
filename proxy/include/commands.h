/**
 * @file commands.h
 * @brief Hierarchical command tree parser and dispatcher.
 *
 * This module implements a generic command interpreter based on a
 * keyword tree (prefix tree / trie). Each command consists of one or
 * multiple elements separated by spaces (tokens). Each token becomes
 * a node in the tree, and a callback is attached to the final node
 * of the command.
 *
 * Features:
 *   - Build a tree of commands from static strings
 *   - Support *literal* keywords (e.g., "show", "bgp", "peers")
 *   - Support *variable* arguments (e.g., "<peer_id>", "<filename>")
 *   - Execute a command by parsing tokens and traversing the tree
 *   - Store arguments into a global `arg[]` array
 *   - Maintain a circular command history
 *
 * Command format examples:
 *
 *      "show bgp routes"
 *      "peer add <asn> <ip>"
 *      "log set <level>"
 *
 * Execution:
 *   - Keywords are matched exactly
 *   - Variable nodes match any token and store their value in arg[]
 *   - If the final node has an associated callback, it is executed:
 *         cb(argc, argv)
 *
 * Memory ownership:
 *   - Nodes and commands allocated via command_new / command_node_new
 *   - Fully freed using command_finish()
 */

#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include "common.h"
#include "debug.h"

/** Maximum length of a full command line. */
#define MAX_COMMAND_SIZE    1024
/** Maximum length of a single token (command element). */
#define MAX_ELEM_SIZE       128
/** Maximum stored history entries. */
#define MAX_HISTORY_SIZE    512

/** Successful command execution. */
#define COMMAND_OK          0
/** Command not found in the tree. */
#define COMMAND_NOT_FOUND   1
/** Callback returned an error. */
#define COMMAND_FAILED      2
/** Command exists but lacks required trailing elements. */
#define COMMAND_INCOMPLETE  3


/**
 * @struct command
 * @brief Represents a fully registered command and its associated callback.
 *
 * `command` strings are stored exactly as provided to add_command_to_tree().
 */
struct command {
    char command[MAX_COMMAND_SIZE]; /**< Full command string. */
    int (*cb)(int argc, char** argv); /**< Callback to run at execution. */
};


/* ---------- Global Variables (used by command interpreter) ---------- */

extern int nb_variable; /**< Number of arguments matched for current command. */
extern char** arg;      /**< Argument array filled during command parsing. */

extern char history[MAX_HISTORY_SIZE][MAX_COMMAND_SIZE]; /**< Circular history buffer. */
extern int history_pos;  /**< Next insertion index in history. */
extern int history_size; /**< Number of valid history entries. */

extern char hist_file[MAX_HISTORY_SIZE + 20]; /**< Optional persistent history file path. */


/**
 * @struct command_node
 * @brief Represents a node in the command tree.
 *
 * Each node corresponds to one token of a command.  
 * Nodes are linked through:
 *   - childs      : first child token (next word in command)
 *   - nextbrother : sibling tokens on same command depth
 *
 * Example:
 *   "show ip bgp" produces nodes:
 *      root -> "show" -> "ip" -> "bgp"
 */
struct command_node {
    char carac[MAX_ELEM_SIZE]; /**< Literal keyword, empty if variable. */
    bool is_variable;          /**< True if this node matches any token (<arg>). */

    struct command_node* nextbrother; /**< Next sibling node. */
    struct command_node* prevbrother; /**< Previous sibling node (unused currently). */
    struct command_node* childs;      /**< First child (next element). */
    struct command_node* parent;      /**< Optional pointer to parent node. */

    struct command* command; /**< Callback/command stored at this node (NULL if intermediate node). */
};


/**
 * @struct command_tree
 * @brief Root container for the full command tree.
 */
struct command_tree {
    struct command_node* root; /**< Root node with empty string. */
};


/* ========================== History ================================= */

/**
 * @brief Add a command to the in-memory history.
 */
extern void add_my_history(char *cmd);

/**
 * @brief Load previous command history from a file into memory.
 */
extern void load_history(char *name);


/* ======================= Initialization / Cleanup ===================== */

/**
 * @brief Initialize command system: allocate tree and argument buffers.
 */
extern void commands_init();

/**
 * @brief Free entire command tree and all command nodes.
 */
extern void free_command_tree(struct command_tree* t);

/**
 * @brief Free a single command structure.
 */
extern void free_command(struct command* cmd);

/**
 * @brief Recursively free a command node and all its children.
 */
extern void free_command_node(struct command_node* node);

/**
 * @brief Free all command structures and argument buffers.
 */
extern void command_finish();


/* ======================= Debug / Introspection ======================== */

/**
 * @brief Print a tree node and all its children, indented.
 */
extern void print_node(struct command_node* node, int offset);

/**
 * @brief Print the entire command tree.
 */
extern void print_tree(struct command_tree* tree);


/* ===================== Command Parsing Helpers ======================== */

/**
 * @brief Extract first token from command string.
 *
 * @param command Full command input buffer.
 * @param elem Output buffer where first token is written.
 * @return Number of characters consumed.
 */
extern int get_first_elem_command(char command[MAX_COMMAND_SIZE], char elem[MAX_ELEM_SIZE]);

/**
 * @brief Remove first nb_char characters from a command string.
 */
extern void remove_n_chars(char command[MAX_COMMAND_SIZE], char new_command[MAX_COMMAND_SIZE], int nb_char);

/**
 * @brief Determine if token is a variable (formatted as `<...>`).
 */
extern bool elem_is_arg(char elem[MAX_ELEM_SIZE]);

/**
 * @brief Debug helper: iterate tokens of a command and detect arguments.
 */
extern void test_decompo(char command[MAX_COMMAND_SIZE]);


/* ======================= Node / Tree Constructors ====================== */

/**
 * @brief Allocate a new command structure and store callback.
 */
extern struct command* command_new(char cmds[MAX_COMMAND_SIZE], int (*cb)(int argc, char** argv));

/**
 * @brief Allocate a new command node.
 *
 * @param carac Literal keyword, ignored if is_arg == True.
 * @param is_arg True if node represents a variable argument.
 */
extern struct command_node* command_node_new(char carac[MAX_ELEM_SIZE], bool is_arg);

/**
 * @brief Allocate a new command tree with a single empty root node.
 */
extern struct command_tree* command_tree_new();


/**
 * @brief Get an existing child node matching token or create it if absent.
 *
 * If is_arg == True:
 *     matches any node with node->is_variable = true.
 * Else:
 *     matches only literal nodes with same keyword.
 */
extern struct command_node* command_node_get_or_add(struct command_node* curr, char elem[MAX_ELEM_SIZE], bool is_arg);

/**
 * @brief Retrieve a child node matching a token.
 *
 * If is_arg == True, match only variable nodes.
 * If is_arg == False, match only literal nodes.
 */
extern struct command_node* command_node_get(struct command_node* curr, char elem[MAX_ELEM_SIZE], bool is_arg);


/* ====================== Command Registration ========================== */

/**
 * @brief Add a new full command to the tree.
 *
 * Example:
 *     add_command_to_tree("show bgp routes", cb_show_routes);
 *
 * @param command Full command string.
 * @param cb Callback associated with final node.
 */
extern int add_command_to_tree(char command[MAX_COMMAND_SIZE], int (*cb)(int argc, char** argv));


/* =========================== Execution ================================ */

/**
 * @brief Parse and execute a command line.
 *
 * @param cmd Input command string.
 * @return COMMAND_OK, COMMAND_NOT_FOUND, COMMAND_INCOMPLETE, COMMAND_FAILED.
 */
extern int execute_command(char cmd[MAX_COMMAND_SIZE]);

/**
 * @brief Print feedback for command execution (error messages).
 */
void print_command_result(int commandSock, int ret, char cmd[1024]);


/* ========================= Global Tree Instance ======================== */

/** Global command tree instance (allocated in commands_init). */
extern struct command_tree* tree;


/* ======================= Command Installer Macro ====================== */

/**
 * @brief Register a command and its callback in a single statement.
 *
 * Usage:
 *
 *      INSTALL_CMD(my, "show bgp peers", cb_show_bgp_peers);
 *
 * Generates:
 *      char my_buf[1024];
 *      strcpy(my_buf, "show bgp peers");
 *      add_command_to_tree(my_buf, cb_show_bgp_peers);
 *
 * Notes:
 *   - The temporary buffer prevents literal strings from being modified.
 */
#define INSTALL_CMD(pref, cmd, cb)          \
    char pref ## _buf[MAX_COMMAND_SIZE];    \
    strcpy(pref ## _buf, cmd);              \
    add_command_to_tree(pref ## _buf, cb);

#endif
