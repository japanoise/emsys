#ifndef KEYMAP_H
#define KEYMAP_H

/* Key constants */
#ifndef CTRL
#define CTRL(x) ((x) & 0x1f)
#endif

/* Prefix state for key sequences */
enum PrefixState { PREFIX_NONE, PREFIX_CTRL_X, PREFIX_CTRL_X_R, PREFIX_META };

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	HOME_KEY,
	DEL_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	UNICODE,
	UNICODE_ERROR,
	END_OF_FILE,
	BEG_OF_FILE,
	QUIT,
	SAVE,
	COPY,
	CUT,
	REDO,
	FORWARD_WORD,
	BACKWARD_WORD,
	FORWARD_PARA,
	BACKWARD_PARA,
	SWITCH_BUFFER,
	NEXT_BUFFER,
	PREVIOUS_BUFFER,
	MARK_BUFFER,
	DELETE_WORD,
	BACKSPACE_WORD,
	OTHER_WINDOW,
	CREATE_WINDOW,
	DESTROY_WINDOW,
	DESTROY_OTHER_WINDOWS,
	KILL_BUFFER,
	MACRO_RECORD,
	MACRO_END,
	MACRO_EXEC,
	ALT_0,
	ALT_1,
	ALT_2,
	ALT_3,
	ALT_4,
	ALT_5,
	ALT_6,
	ALT_7,
	ALT_8,
	ALT_9,
	SUSPEND,
	UPCASE_WORD,
	DOWNCASE_WORD,
	CAPCASE_WORD,
	UPCASE_REGION,
	DOWNCASE_REGION,
	TOGGLE_TRUNCATE_LINES,
	TRANSPOSE_WORDS,
	EXEC_CMD,
	FIND_FILE,
	WHAT_CURSOR,
	PIPE_CMD,
	CUSTOM_INFO_MESSAGE,
	QUERY_REPLACE,
	GOTO_LINE,
	BACKTAB,
	SWAP_MARK,
	JUMP_REGISTER,
	MACRO_REGISTER,
	POINT_REGISTER,
	NUMBER_REGISTER,
	REGION_REGISTER,
	INC_REGISTER,
	INSERT_REGISTER,
	VIEW_REGISTER,
	STRING_RECT,
	COPY_RECT,
	KILL_RECT,
	YANK_RECT,
	RECT_REGISTER,
	EXPAND,
	UNIVERSAL_ARGUMENT,
};

/* Forward declarations */
struct editorBuffer;
struct editorMacro;
struct editorConfig;

/* Function declarations */
void editorRecordKey(int c);
void editorProcessKeypress(int c);
void editorExecMacro(struct editorMacro *macro);
void setupCommands(struct editorConfig *ed);
void runCommand(char *cmd, struct editorConfig *ed, struct editorBuffer *buf);
void executeCommand(int key);
void showPrefix(const char *prefix);

#endif /* KEYMAP_H */