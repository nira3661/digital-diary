#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

// ---- Diary / Habit / UI Control IDs ----
#define IDC_ENTRYBOX    101
#define IDC_VIEWBOX     102
#define IDC_ADD         103
#define IDC_VIEW        104
#define IDC_DELETE      105
#define IDC_TIMESTAMP   200
#define IDC_OK          201
#define IDC_HABIT1      300
#define IDC_HABIT2      301
#define IDC_HABIT3      302
#define IDC_VIEWHABITS  303

// ---- Task Bar Controls ----
#define IDC_TASKINPUT   400
#define IDC_ADDTASK     401
#define IDC_VIEWTASKS   402

// ---- Fixed Workspace Slots (shown on main window) ----
#define IDC_WORKSPACE1  500
#define IDC_WORKSPACE2  501

// ---- Task Manager Dialog Controls ----
#define DLG_PENDING_LIST  1001
#define DLG_DONE_LIST     1002
#define DLG_ADD_BTN       1003
#define DLG_CLOSE_BTN     1004

// ---- Custom Window Message ----
#define WM_ADDTOWORKSPACE  (WM_APP + 1)

// ---- Task Limits ----
#define MAX_PENDING    30
#define MAX_COMPLETED  25
#define TASKS_FILE     "tasks.txt"

// ---- Task Structure ----
typedef struct { char name[128]; char date[20]; } Task;

static Task pendingTasks[35];
static Task completedTasks[30];
static int  pendingCount   = 0;
static int  completedCount = 0;

// ---- Dialog State (set before sending WM_ADDTOWORKSPACE) ----
static HWND hTaskDlg   = NULL;
static char dlgSelected[2][128];
static int  dlgSelCount = 0;

// ---- Workspace checkbox handles ----
static HWND hWorkspace[2] = {NULL, NULL};

// ---- Forward Declarations ----
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TaskDlgProc(HWND, UINT, WPARAM, LPARAM);
void LoadTasks(void);
void SaveTasks(void);
void LoadHabitStatus(HWND);
void RefreshTaskLists(HWND);


void LoadHabitStatus(HWND hwnd) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char date[20];
    strftime(date, sizeof(date), "%Y-%m-%d", tm_info);

    // Default: reset all to unchecked
    SendMessage(GetDlgItem(hwnd, IDC_HABIT1), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwnd, IDC_HABIT2), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwnd, IDC_HABIT3), BM_SETCHECK, BST_UNCHECKED, 0);

    FILE *f = fopen("habits.txt", "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char existingDate[20], existingHabit[128], status[16];
        if (sscanf(line, "%19[^|]|%127[^|]|%15s", existingDate, existingHabit, status) == 3) {
            if (strcmp(existingDate, date) == 0) {
                int checkState = (strcmp(status, "Done") == 0) ? BST_CHECKED : BST_UNCHECKED;
                if (strcmp(existingHabit, "Exercise") == 0)
                    SendMessage(GetDlgItem(hwnd, IDC_HABIT1), BM_SETCHECK, checkState, 0);
                else if (strcmp(existingHabit, "Read") == 0)
                    SendMessage(GetDlgItem(hwnd, IDC_HABIT2), BM_SETCHECK, checkState, 0);
                else if (strcmp(existingHabit, "Meditate") == 0)
                    SendMessage(GetDlgItem(hwnd, IDC_HABIT3), BM_SETCHECK, checkState, 0);
            }
        }
    }
    fclose(f);
}





void RefreshTaskLists(HWND hwnd) {
    LoadTasks(); // reload from disk

    HWND hPending = GetDlgItem(hwnd, DLG_PENDING_LIST);
    HWND hDone    = GetDlgItem(hwnd, DLG_DONE_LIST);

    // Clear old contents
    SendMessage(hPending, LB_RESETCONTENT, 0, 0);
    SendMessage(hDone, LB_RESETCONTENT, 0, 0);

    // Refill pending
    for (int i = 0; i < pendingCount; i++)
        SendMessage(hPending, LB_ADDSTRING, 0, (LPARAM)pendingTasks[i].name);
    if (pendingCount == 0)
        SendMessage(hPending, LB_ADDSTRING, 0, (LPARAM)"(No pending tasks)");

    // Refill completed (newest first)
    for (int i = completedCount - 1; i >= 0; i--) {
        char display[160];
        snprintf(display, sizeof(display), "[%s]  %s",
                 completedTasks[i].date, completedTasks[i].name);
        SendMessage(hDone, LB_ADDSTRING, 0, (LPARAM)display);
    }
    if (completedCount == 0)
        SendMessage(hDone, LB_ADDSTRING, 0, (LPARAM)"(No completed tasks yet)");
}



// =============================================================
//  TASK PERSISTENCE
// =============================================================

void LoadTasks(void) {
    pendingCount = completedCount = 0;
    FILE *f = fopen(TASKS_FILE, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "PENDING|", 8) == 0 && pendingCount < 35) {
            strncpy(pendingTasks[pendingCount].name, line + 8, 127);
            pendingTasks[pendingCount].name[127] = 0;
            pendingTasks[pendingCount].date[0]   = 0;
            pendingCount++;
        } else if (strncmp(line, "DONE|", 5) == 0 && completedCount < 30) {
            char *p   = line + 5;
            char *bar = strchr(p, '|');
            if (bar) {
                *bar = 0;
                strncpy(completedTasks[completedCount].date, p, 19);
                completedTasks[completedCount].date[19] = 0;
                strncpy(completedTasks[completedCount].name, bar + 1, 127);
                completedTasks[completedCount].name[127] = 0;
            } else {
                strncpy(completedTasks[completedCount].name, p, 127);
                completedTasks[completedCount].date[0] = 0;
            }
            completedCount++;
        }
    }
    fclose(f);
}

void SaveTasks(void) {
    /* Keep only the most recent MAX_COMPLETED completed tasks */
    int startDone = (completedCount > MAX_COMPLETED)
                    ? completedCount - MAX_COMPLETED : 0;

    FILE *f = fopen(TASKS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < pendingCount; i++)
        fprintf(f, "PENDING|%s\n", pendingTasks[i].name);
    for (int i = startDone; i < completedCount; i++)
        fprintf(f, "DONE|%s|%s\n", completedTasks[i].date, completedTasks[i].name);
    fclose(f);

    /* Trim in-memory completed list */
    if (completedCount > MAX_COMPLETED) {
        int excess = completedCount - MAX_COMPLETED;
        memmove(completedTasks, completedTasks + excess,
                MAX_COMPLETED * sizeof(Task));
        completedCount = MAX_COMPLETED;
    }
}

void AddTaskToList(const char *name) {
    /* Caller must verify pendingCount < MAX_PENDING first */
    strncpy(pendingTasks[pendingCount].name, name, 127);
    pendingTasks[pendingCount].name[127] = 0;
    pendingTasks[pendingCount].date[0]   = 0;
    pendingCount++;
    SaveTasks();
}

void MarkTaskDoneByName(const char *name) {
    for (int i = 0; i < pendingCount; i++) {
        if (strcmp(pendingTasks[i].name, name) != 0) continue;

        /* Stamp completion date */
        time_t t = time(NULL);
        struct tm *ti = localtime(&t);
        char date[20];
        strftime(date, sizeof(date), "%Y-%m-%d", ti);

        if (completedCount < 30) {
            strncpy(completedTasks[completedCount].name, name, 127);
            strncpy(completedTasks[completedCount].date, date, 19);
            completedCount++;
        }

        /* Remove from pending */
        memmove(&pendingTasks[i], &pendingTasks[i + 1],
                (pendingCount - i - 1) * sizeof(Task));
        pendingCount--;
        SaveTasks();
        return;
    }
}


// =============================================================
//  TASK MANAGER DIALOG
// =============================================================

LRESULT CALLBACK TaskDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

    switch (msg) {

    case WM_CREATE: {
        /* ---- Pending Tasks section ---- */
        CreateWindow("STATIC",
                     "Pending Tasks  \x97  select up to 2 to work on:",
                     WS_CHILD | WS_VISIBLE,
                     10, 10, 410, 18, hwnd, NULL, hInst, NULL);

        HWND hPending = CreateWindow("LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            LBS_MULTIPLESEL | LBS_NOTIFY,
            10, 32, 410, 155,
            hwnd, (HMENU)DLG_PENDING_LIST, hInst, NULL);

        for (int i = 0; i < pendingCount; i++)
            SendMessage(hPending, LB_ADDSTRING, 0, (LPARAM)pendingTasks[i].name);
        if (pendingCount == 0)
            SendMessage(hPending, LB_ADDSTRING, 0, (LPARAM)"(No pending tasks)");

        /* ---- Completed Tasks section ---- */
        CreateWindow("STATIC",
                     "Completed Tasks  (most recent 25, newest first):",
                     WS_CHILD | WS_VISIBLE,
                     10, 200, 410, 18, hwnd, NULL, hInst, NULL);

        HWND hDone = CreateWindow("LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOSEL,
            10, 222, 410, 155,
            hwnd, (HMENU)DLG_DONE_LIST, hInst, NULL);

        /* Show newest completed task first */
        for (int i = completedCount - 1; i >= 0; i--) {
            char display[160];
            snprintf(display, sizeof(display), "[%s]  %s",
                     completedTasks[i].date, completedTasks[i].name);
            SendMessage(hDone, LB_ADDSTRING, 0, (LPARAM)display);
        }
        if (completedCount == 0)
            SendMessage(hDone, LB_ADDSTRING, 0, (LPARAM)"(No completed tasks yet)");

        /* ---- Buttons ---- */
        CreateWindow("BUTTON", "Add Selected to Workspace",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            10, 392, 210, 30,
            hwnd, (HMENU)DLG_ADD_BTN, hInst, NULL);

        CreateWindow("BUTTON", "Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            235, 392, 80, 30,
            hwnd, (HMENU)DLG_CLOSE_BTN, hInst, NULL);
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case DLG_ADD_BTN: {
            HWND hList   = GetDlgItem(hwnd, DLG_PENDING_LIST);
            int selCount = (int)SendMessage(hList, LB_GETSELCOUNT, 0, 0);

            if (selCount == 0) {
                MessageBox(hwnd,
                    "Please select at least one task to work on.",
                    "No Selection", MB_OK | MB_ICONINFORMATION);
                break;
            }
            if (selCount > 2) {
                MessageBox(hwnd,
                    "You may add a maximum of 2 tasks to your active workspace at a time.\n"
                    "Please deselect some tasks and try again.",
                    "Selection Limit Exceeded", MB_OK | MB_ICONWARNING);
                break;
            }

            int indices[2] = {-1, -1};
            SendMessage(hList, LB_GETSELITEMS, 2, (LPARAM)indices);
            dlgSelCount = 0;
            for (int i = 0; i < selCount && i < 2; i++) {
                SendMessage(hList, LB_GETTEXT, indices[i],
                            (LPARAM)dlgSelected[dlgSelCount++]);
            }

            /* Notify main window, then close dialog */
            SendMessage(GetParent(hwnd), WM_ADDTOWORKSPACE, 0, 0);
            DestroyWindow(hwnd);
            EnableWindow(GetParent(hwnd), TRUE);
            break;
        }

        case DLG_CLOSE_BTN:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        EnableWindow(GetParent(hwnd), TRUE);
        SetForegroundWindow(GetParent(hwnd));
        hTaskDlg = NULL;
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowAllTasksDialog(HWND hParent) {
    /* Prevent duplicate dialog windows */
    if (hTaskDlg) { SetForegroundWindow(hTaskDlg); return; }

    LoadTasks();   /* Always refresh from disk before showing */

    static BOOL classReg = FALSE;
    if (!classReg) {
        WNDCLASS wc      = {0};
        wc.lpfnWndProc   = TaskDlgProc;
        wc.hInstance     = (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE);
        wc.lpszClassName = "TaskDialog";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
        classReg = TRUE;
    }

    hTaskDlg = CreateWindow("TaskDialog", "Task Manager",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        150, 80, 450, 465,
        hParent, NULL,
        (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE), NULL);

}


// =============================================================
//  HABITS
// =============================================================

void SaveHabitStatus(HWND hwnd, int id, const char *habit) {
    BOOL checked = SendMessage(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0);

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char date[20];
    strftime(date, sizeof(date), "%Y-%m-%d", tm_info);

    FILE *f = fopen("habits.txt", "r");
    char buffer[8192] = "";
    char line[256];
    int found = 0;

    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char existingDate[20], existingHabit[128], status[16];
            if (sscanf(line, "%19[^|]|%127[^|]|%15s", existingDate, existingHabit, status) == 3) {
                if (strcmp(existingDate, date) == 0 && strcmp(existingHabit, habit) == 0) {
                    // Replace with new state
                    sprintf(line, "%s|%s|%s\n", date, habit, checked ? "Done" : "NotDone");
                    found = 1;
                }
            }
            strcat(buffer, line);
        }
        fclose(f);
    }

    if (!found) {
        char newLine[256];
        sprintf(newLine, "%s|%s|%s\n", date, habit, checked ? "Done" : "NotDone");
        strcat(buffer, newLine);
    }

    f = fopen("habits.txt", "w");
    if (f) {
        fputs(buffer, f);
        fclose(f);
    }
}




void ViewHabitList(HWND hwnd) {
    FILE *f = fopen("habits.txt", "r");
    if (!f) { MessageBox(hwnd, "No habit history yet.", "Habits", MB_OK); return; }
    char buffer[4096] = "";
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f) && count < 100) {
        strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
        count++;
    }
    fclose(f);
    MessageBox(hwnd, buffer, "Habit List (Last 2 Weeks)", MB_OK);
}


// =============================================================
//  DIARY
// =============================================================

void DeleteEntryByTime(HWND hwnd) {
    char target[64];
    GetWindowText(GetDlgItem(hwnd, IDC_TIMESTAMP), target, sizeof(target));
    if (strlen(target) == 0) {
        MessageBox(hwnd, "Enter a timestamp!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    FILE *f = fopen("diary.txt", "r");
    if (!f) {
        MessageBox(hwnd, "No diary file found.", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    char buffer[8192] = "";
    char line[512];
    int skip = 0;
while (fgets(line, sizeof(line), f)) {
    if (!skip && line[0] == '[' && strstr(line, target)) {
        skip = 1; // start skipping this entry
        continue;
    }
    if (skip && line[0] == '[') {
        skip = 0; // new entry begins, stop skipping
    }
    if (!skip) {
        strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
    }
}

    fclose(f);
    f = fopen("diary.txt", "w");
    if (f) { fputs(buffer, f); fclose(f); MessageBox(hwnd, "Entry deleted.", "Diary", MB_OK); }
}

void SaveDiaryEntry(HWND hwnd) {
    char text[512];
    GetWindowText(GetDlgItem(hwnd, IDC_ENTRYBOX), text, sizeof(text));
    if (strlen(text) == 0) {
        MessageBox(hwnd, "Entry cannot be empty!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    FILE *f = fopen("diary.txt", "a");
    if (f) {
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        fprintf(f, "[%s] %s\r\n", timestamp, text);
        fclose(f);
    }
    SetWindowText(GetDlgItem(hwnd, IDC_ENTRYBOX), "");
    MessageBox(hwnd, "Entry added!", "Diary", MB_OK);
}

void ViewEntries(HWND hwnd) {
    FILE *f = fopen("diary.txt", "r");
    if (!f) { SetWindowText(GetDlgItem(hwnd, IDC_VIEWBOX), "No entries yet."); return; }
    char buffer[4096] = "";
    char line[512];
    while (fgets(line, sizeof(line), f)) {
    strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
    strncat(buffer, "\r\n", sizeof(buffer) - strlen(buffer) - 1);
}

    fclose(f);
    SetWindowText(GetDlgItem(hwnd, IDC_VIEWBOX), buffer);
}

void DeleteEntries(HWND hwnd) {
    int result = MessageBox(hwnd, "Are you sure you want to delete ALL entries?",
                            "Confirm Delete", MB_YESNO | MB_ICONWARNING);
    if (result == IDYES) {
        FILE *f = fopen("diary.txt", "w");
        if (f) fclose(f);
        SetWindowText(GetDlgItem(hwnd, IDC_VIEWBOX), "");
        MessageBox(hwnd, "All entries deleted.", "Diary", MB_OK);
    }
}

// Global flag and password
static int passwordOK = 0;
static const char *CORRECT_PASSWORD = "mypassword";  // Change this to your desired password

LRESULT CALLBACK PasswordWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        
        CreateWindow("STATIC", "Enter Password to Access App:", WS_CHILD|WS_VISIBLE,
                     15, 15, 240, 20, hwnd, NULL, hInst, NULL);
        CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_PASSWORD,
                     15, 40, 240, 25, hwnd, (HMENU)1, hInst, NULL);
        CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
                     15, 75, 110, 30, hwnd, (HMENU)2, hInst, NULL);
        CreateWindow("BUTTON", "Exit", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                     135, 75, 120, 30, hwnd, (HMENU)3, hInst, NULL);
        
        // Set focus to password field
        SetFocus(GetDlgItem(hwnd, 1));
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 2) { // OK button
            char buf[64] = "";
            GetWindowText(GetDlgItem(hwnd, 1), buf, sizeof(buf));
            if (strcmp(buf, CORRECT_PASSWORD) == 0) {
                passwordOK = 1;
                DestroyWindow(hwnd);
            } else {
                MessageBox(hwnd, "Wrong password! Please try again.", "Access Denied", MB_OK|MB_ICONERROR);
                // Clear password field and refocus
                SetWindowText(GetDlgItem(hwnd, 1), "");
                SetFocus(GetDlgItem(hwnd, 1));
            }
        } else if (LOWORD(wParam) == 3) { // Exit button
            DestroyWindow(hwnd);
        }
        break;
        
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            // Pressing Enter = OK
            SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(2, BN_CLICKED), 0);
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


// =============================================================
//  WINMAIN
// =============================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {

    // Register PasswordDialog window class
    WNDCLASS wcPass = {0};
    wcPass.lpfnWndProc   = PasswordWndProc;
    wcPass.hInstance     = hInstance;
    wcPass.lpszClassName = "PasswordDialog";
    wcPass.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wcPass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wcPass);

    // Create and show password dialog
    HWND hPassDlg = CreateWindow("PasswordDialog", "Diary App Login",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        400, 250, 280, 150, NULL, NULL, hInstance, NULL);

    ShowWindow(hPassDlg, nCmdShow);
    UpdateWindow(hPassDlg);

    MSG msg = {0};
    
    // Message loop for password dialog
    while (IsWindow(hPassDlg) && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // If password not correct, exit application
    if (!passwordOK) {
        MessageBox(NULL, "Access denied. Application closing.", "Login Failed", MB_OK|MB_ICONINFORMATION);
        return 0;
    }

    // Register main DiaryApp window class
    WNDCLASS wcApp = {0};
    wcApp.lpfnWndProc   = WndProc;
    wcApp.hInstance     = hInstance;
    wcApp.lpszClassName = "DiaryApp";
    wcApp.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wcApp.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcApp.style         = CS_VREDRAW | CS_HREDRAW;
    RegisterClass(&wcApp);

    // Create main application window
    HWND hwnd = CreateWindow("DiaryApp", "Interstitial Journaling",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             600, 560, NULL, NULL, hInstance, NULL);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

CreateWindow("BUTTON", "Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             460, 460, 80, 25, hwnd, (HMENU)600, hInstance, NULL);

    /* Timestamp input + OK (delete-by-time) */
    CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER,
                 400, 20, 120, 25, hwnd, (HMENU)IDC_TIMESTAMP, hInstance, NULL);
    CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 530, 20, 50, 25, hwnd, (HMENU)IDC_OK, hInstance, NULL);

    /* Diary buttons */
    CreateWindow("BUTTON", "Add Entry", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                 20, 20, 100, 30, hwnd, (HMENU)IDC_ADD, hInstance, NULL);
    CreateWindow("BUTTON", "View Entries", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 140, 20, 100, 30, hwnd, (HMENU)IDC_VIEW, hInstance, NULL);
    CreateWindow("BUTTON", "Delete Entries", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 260, 20, 120, 30, hwnd, (HMENU)IDC_DELETE, hInstance, NULL);

    /* Entry input box */
    CreateWindow("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
        20, 60, 540, 80, hwnd, (HMENU)IDC_ENTRYBOX, hInstance, NULL);

    /* View box */
    CreateWindow("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        20, 150, 540, 265, hwnd, (HMENU)IDC_VIEWBOX, hInstance, NULL);

    /* Habit checkboxes */
    CreateWindow("BUTTON", "Exercise", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                 20, 428, 100, 20, hwnd, (HMENU)IDC_HABIT1, hInstance, NULL);
    CreateWindow("BUTTON", "Read",     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                 20, 454, 100, 20, hwnd, (HMENU)IDC_HABIT2, hInstance, NULL);
    CreateWindow("BUTTON", "Meditate", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                 20, 480, 100, 20, hwnd, (HMENU)IDC_HABIT3, hInstance, NULL);

    /* View Habit List button */
    CreateWindow("BUTTON", "View Habit List", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 140, 428, 120, 28, hwnd, (HMENU)IDC_VIEWHABITS, hInstance, NULL);

    /* "Manage Tasks" button (replaces old "View Completed Tasks") */
    CreateWindow("BUTTON", "Manage Tasks", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 140, 462, 120, 28, hwnd, (HMENU)IDC_VIEWTASKS, hInstance, NULL);

    /* Task input bar */
CreateWindow("EDIT", "", 
    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
    300, 428, 150, 25, hwnd, (HMENU)IDC_TASKINPUT, hInstance, NULL);




    /* Add Task button */
    CreateWindow("BUTTON", "Add Task", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 460, 428, 80, 25, hwnd, (HMENU)IDC_ADDTASK, hInstance, NULL);

    /* Main application message loop */
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}


// =============================================================
//  WINDOW PROCEDURE
// =============================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    switch (msg) {

    /* ---- Initialise: load tasks from disk on startup ---- */
case WM_CREATE:
    LoadTasks();
    LoadHabitStatus(hwnd); // restore today’s habit state
    break;



    /* ---- Custom message: place selected tasks in workspace slots ---- */
case WM_ADDTOWORKSPACE: {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

    // Clear any existing workspace checkboxes first
    for (int i = 0; i < 2; i++) {
        if (hWorkspace[i]) { DestroyWindow(hWorkspace[i]); hWorkspace[i] = NULL; }
    }

    // Create a checkbox for each selected task
    for (int i = 0; i < dlgSelCount && i < 2; i++) {
        hWorkspace[i] = CreateWindow("BUTTON", dlgSelected[i],
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            300, 460 + i * 26, 245, 20,
            hwnd, (HMENU)(IDC_WORKSPACE1 + i), hInst, NULL);
    }
    break;
}

case WM_KEYDOWN:
    if (wParam == VK_RETURN) {
        HWND hFocus = GetFocus();
        if (hFocus == GetDlgItem(hwnd, IDC_TASKINPUT)) {
            // Simulate clicking Add Task
            SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_ADDTASK, BN_CLICKED),
                        (LPARAM)GetDlgItem(hwnd, IDC_ADDTASK));
            return 0; // swallow the Enter key
        }
    }
    break;


    case WM_COMMAND:
        switch (LOWORD(wParam)) {

case IDC_TASKINPUT: {
    if (HIWORD(wParam) == EN_UPDATE) {
        // optional: live validation
    }
    break;
}




case 600: { // Refresh button
    // Step 1: mark checked workspace tasks as done
    for (int i = 0; i < 2; i++) {
        if (hWorkspace[i] && SendMessage(hWorkspace[i], BM_GETCHECK, 0, 0) == BST_CHECKED) {
            char taskText[128];
            GetWindowText(hWorkspace[i], taskText, sizeof(taskText));
            MarkTaskDoneByName(taskText);
            DestroyWindow(hWorkspace[i]);
            hWorkspace[i] = NULL;
        }
    }

    // Step 2: refresh Task Manager lists if open
    if (hTaskDlg) RefreshTaskLists(hTaskDlg);

    // Step 3: reload tasks from disk so we know which are still pending
    LoadTasks();

    // Step 4: rebuild workspace checkboxes ONLY for tasks still pending
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    for (int i = 0; i < dlgSelCount && i < 2; i++) {
        // Check if dlgSelected[i] is still in pendingTasks[]
        int stillPending = 0;
        for (int j = 0; j < pendingCount; j++) {
            if (strcmp(dlgSelected[i], pendingTasks[j].name) == 0) {
                stillPending = 1;
                break;
            }
        }

        if (stillPending && !hWorkspace[i]) {
            hWorkspace[i] = CreateWindow("BUTTON", dlgSelected[i],
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                300, 460 + i * 26, 245, 20,
                hwnd, (HMENU)(IDC_WORKSPACE1 + i), hInst, NULL);
        }
    }

    break;
}






        /* ---- Diary controls ---- */
        case IDC_ADD:    SaveDiaryEntry(hwnd);   break;
        case IDC_VIEW:   ViewEntries(hwnd);      break;
        case IDC_DELETE: DeleteEntries(hwnd);    break;
        case IDC_OK:     DeleteEntryByTime(hwnd); break;

        /* ---- Habit controls ---- */
case IDC_HABIT1:
    SaveHabitStatus(hwnd, IDC_HABIT1, "Exercise");
    break;

case IDC_HABIT2:
    SaveHabitStatus(hwnd, IDC_HABIT2, "Read");
    break;

case IDC_HABIT3:
    SaveHabitStatus(hwnd, IDC_HABIT3, "Meditate");
    break;

        case IDC_VIEWHABITS:
            ViewHabitList(hwnd);
            break;

        /* ---- Add Task ---- */
        case IDC_ADDTASK: {
            LoadTasks(); /* Refresh count from disk before checking limit */

            if (pendingCount >= MAX_PENDING) {
                MessageBox(hwnd,
                    "Your task list has reached its limit of 30 items.\n\n"
                    "Please complete or remove existing tasks before adding new ones.",
                    "Task List Full", MB_OK | MB_ICONWARNING);
                break;
            }

            char taskName[128];
            GetWindowText(GetDlgItem(hwnd, IDC_TASKINPUT), taskName, sizeof(taskName));
            if (strlen(taskName) > 0) {
                AddTaskToList(taskName);
                SetWindowText(GetDlgItem(hwnd, IDC_TASKINPUT), "");
            }
            break;
        }

        /* ---- Manage Tasks (open dialog) ---- */
        case IDC_VIEWTASKS:
            ShowAllTasksDialog(hwnd);
            break;

        /* ---- Workspace slot 1 ---- */
case IDC_WORKSPACE1: {
    if (hWorkspace[0] && SendMessage(hWorkspace[0], BM_GETCHECK, 0, 0) == BST_CHECKED) {
        char taskText[128];
        GetWindowText(hWorkspace[0], taskText, sizeof(taskText));
        MarkTaskDoneByName(taskText);

        // Show completion in UI
        char completedText[160];
        snprintf(completedText, sizeof(completedText), "✔ Completed: %s", taskText);
        SetWindowText(hWorkspace[0], completedText);
        EnableWindow(hWorkspace[0], FALSE);

        MessageBox(hwnd,
            "Task marked as complete and moved to your completed list.",
            "Task Complete", MB_OK | MB_ICONINFORMATION);

        if (hTaskDlg) RefreshTaskLists(hTaskDlg); // update Task Manager lists
    }
    break;
}

case IDC_WORKSPACE2: {
    if (hWorkspace[1] && SendMessage(hWorkspace[1], BM_GETCHECK, 0, 0) == BST_CHECKED) {
        char taskText[128];
        GetWindowText(hWorkspace[1], taskText, sizeof(taskText));
        MarkTaskDoneByName(taskText);

        // Show completion in UI
        char completedText[160];
        snprintf(completedText, sizeof(completedText), "✔ Completed: %s", taskText);
        SetWindowText(hWorkspace[1], completedText);
        EnableWindow(hWorkspace[1], FALSE);

        MessageBox(hwnd,
            "Task marked as complete and moved to your completed list.",
            "Task Complete", MB_OK | MB_ICONINFORMATION);

        if (hTaskDlg) RefreshTaskLists(hTaskDlg); // update Task Manager lists
    }
    break;
}


        } /* end inner switch */
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}