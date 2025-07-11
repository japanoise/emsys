#ifndef EMSYS_COMPLETION_H
#define EMSYS_COMPLETION_H

#include "emsys.h"

void resetCompletionState(struct completion_state *state);
void freeCompletionResult(struct completion_result *result);
void getFileCompletions(const char *prefix, struct completion_result *result);
void getBufferCompletions(struct editorConfig *ed, const char *prefix, 
                         struct editorBuffer *currentBuffer, 
                         struct completion_result *result);
void getCommandCompletions(struct editorConfig *ed, const char *prefix, 
                          struct completion_result *result);
void handleMinibufferCompletion(struct editorBuffer *minibuf, enum promptType type);
char *findCommonPrefix(char **strings, int count);
void closeCompletionsBuffer(void);
void editorCompleteWord(struct editorConfig *ed, struct editorBuffer *bufr);

#endif