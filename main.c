#include <stdbool.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <leif/leif.h>
#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>


#include "config.h"

typedef enum{
    TAB_DASHBOARD = 0,
    TAB_NEW_TASK
} gui_tab;


typedef enum {
  FILTER_ALL = 0,
  FILTER_IN_PROGRESS,
  FILTER_COMPLETED,
  FILTER_LOW,
  FILTER_MEDIUM,
  FILTER_HIGH
} todo_filter;

typedef enum {
  PRIORITY_LOW = 0,
  PRIORITY_MEDIUM,
  PRIORITY_HIGH
} entry_priority;

typedef struct {
  bool completed;
  char* desc, *date;
  entry_priority priority;
} todo_entry;

static LfFont titlefont, smallfont;
static todo_filter current_filter;
static gui_tab current_tab;

static todo_entry* entries[1024];
static uint32_t numEntries = 0;

static LfTexture removeTexture, backTexture;

static LfInputField new_task_input;
static char new_task_input_buf[INPUT_BUF_SIZE];

static int32_t selected_priority = -1;


void save_entries_to_json(const char *filename) {
    cJSON *json_entries = cJSON_CreateArray();
    for (uint32_t i = 0; i < numEntries; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddBoolToObject(entry, "completed", entries[i]->completed);
        cJSON_AddStringToObject(entry, "desc", entries[i]->desc);
        cJSON_AddStringToObject(entry, "date", entries[i]->date);
        cJSON_AddNumberToObject(entry, "priority", entries[i]->priority);
        cJSON_AddItemToArray(json_entries, entry);
    }

    char *string = cJSON_Print(json_entries);
    FILE *file = fopen(filename, "w");
    if (file) {
        fputs(string, file);
        fclose(file);
    }

    cJSON_Delete(json_entries);
    free(string);
}


void load_entries_from_json(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = malloc(length + 1);
    fread(data, 1, length, file);
    fclose(file);

    data[length] = '\0';

    cJSON *json_entries = cJSON_Parse(data);
    if (!cJSON_IsArray(json_entries)) {
        cJSON_Delete(json_entries);
        free(data);
        return;
    }

    numEntries = cJSON_GetArraySize(json_entries);
    for (uint32_t i = 0; i < numEntries; i++) {
        cJSON *json_entry = cJSON_GetArrayItem(json_entries, i);
        todo_entry *entry = malloc(sizeof(todo_entry));
        entry->completed = cJSON_IsTrue(cJSON_GetObjectItem(json_entry, "completed"));
        entry->desc = strdup(cJSON_GetObjectItem(json_entry, "desc")->valuestring);
        entry->date = strdup(cJSON_GetObjectItem(json_entry, "date")->valuestring);
        entry->priority = cJSON_GetObjectItem(json_entry, "priority")->valueint;
        entries[i] = entry;
    }

    cJSON_Delete(json_entries);
    free(data);
}



char* get_command_output(const char* cmd) {
    FILE *fp;
    char buffer[1024];
    char *result = NULL;
    size_t result_size = 0;

    // Opening a new pipe with the fiven command
    fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("Failed to run command\n");
        return NULL;
    }

    // Reading the output
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t buffer_len = strlen(buffer);
        char *temp = realloc(result, result_size + buffer_len + 1);
        if (temp == NULL) {
            printf("Memory allocation failed\n");
            free(result);
            pclose(fp);
            return NULL;
        }
        result = temp;
        strcpy(result + result_size, buffer);
        result_size += buffer_len;
    }
    pclose(fp);
    return result;
}

static int compare_entry_priority(const void* a, const void* b)
{
    todo_entry* entry_a = *(todo_entry**)a;
    todo_entry* entry_b = *(todo_entry**)b;
    return (entry_b->priority - entry_a->priority);
}

static void sort_entries_by_priority(todo_entry** entries){
    qsort(entries, numEntries, sizeof(todo_entry*), compare_entry_priority);
}

static void rendertopbar() {
    lf_push_font(&titlefont);
    {
        LfUIElementProps props = lf_get_theme().text_props;
        lf_push_style_props(props);
        lf_text("Kurisu To do");
        lf_pop_style_props();
    }
    lf_pop_font();
    
    const float width = 160.0f;

    lf_set_ptr_x_absolute(WIN_INIT_W - width - GLOBAL_MARGIN * 2.0f);
    LfUIElementProps props = lf_get_theme().button_props;
    props.margin_left = 0.0f;
    props.margin_right = 0.0f;
    props.color = SECONDARY_COLOR;
    props.border_width = 0.0f;
    props.corner_radius = 4.0f;

    lf_push_style_props(props);
    lf_set_line_should_overflow(false);
    if(lf_button_fixed("New task", width, -1) == LF_CLICKED){
        current_tab = TAB_NEW_TASK;
    }
    lf_set_line_should_overflow(true);
    lf_pop_style_props();
}

static void renderfilters() {
    uint32_t numfilters = 6;
    static const char* filters[] = {"ALL", "IN PROGRESS", "COMPLETED", "LOW", "MEDIUM", "HIGH"};

    LfUIElementProps props = lf_get_theme().button_props;
    props.margin_top = 30.0f;
    props.margin_right = 10.0f;
    props.margin_left = 10.0f;
    props.padding = 10.0f;
    props.border_width = 0.0f;
    props.color = LF_NO_COLOR;
    props.text_color = LF_WHITE;
    props.corner_radius = 8.0f;
    float width = 0.0f;
    float ptrx_before = lf_get_ptr_x();
    float ptry_before = lf_get_ptr_y();
    lf_push_style_props(props);
    lf_set_no_render(true);
    lf_set_ptr_y_absolute(lf_get_ptr_y() + 50.0f);

    for (uint32_t i = 0; i < numfilters; i++) {
        lf_button(filters[i]);
    }
    lf_set_no_render(false);
    lf_set_ptr_y_absolute(ptry_before);
    width = lf_get_ptr_x() - ptrx_before - props.margin_right - props.padding;

    lf_set_ptr_x_absolute(WIN_INIT_W - width - GLOBAL_MARGIN);

    lf_set_line_should_overflow(false);
    for (uint32_t i = 0; i < numfilters; i++) {
        props.color = (current_filter == i) ? (LfColor){255, 255, 255, 50} : LF_NO_COLOR;
        lf_push_style_props(props);
        if (lf_button(filters[i]) == LF_CLICKED) {
            current_filter = (todo_filter)i;
        }
        lf_pop_style_props();
    }
    lf_set_line_should_overflow(true);
    lf_pop_style_props();  // Restaurar el estilo después de terminar los filtros
}

static void renderentries()
{ 

    lf_div_begin(((vec2s){lf_get_ptr_x(), lf_get_ptr_y()}), ((vec2s){WIN_INIT_W - lf_get_ptr_x() - GLOBAL_MARGIN, WIN_INIT_H - lf_get_ptr_y() - GLOBAL_MARGIN}), true);

    uint32_t renderedcount = 0;
    float start_x = lf_get_ptr_x();

    for (uint32_t i = 0; i < numEntries; i++)
    {
        todo_entry* entry = entries[i];
        if(current_filter == FILTER_LOW && entry->priority != PRIORITY_LOW) continue;
        if(current_filter == FILTER_MEDIUM && entry->priority != PRIORITY_MEDIUM) continue;
        if(current_filter == FILTER_HIGH && entry->priority != PRIORITY_HIGH) continue;
        if(current_filter == FILTER_COMPLETED && !entry->completed) continue;
        if(current_filter == FILTER_IN_PROGRESS && entry->completed) continue;
        lf_set_ptr_x_absolute(start_x);
        float priority_size = 15.0f;
        float ptry_before = lf_get_ptr_y();
        lf_set_ptr_y_absolute(lf_get_ptr_y() + 5.0f);
        lf_set_ptr_x_absolute(lf_get_ptr_x() + 5.0f);

        bool clicked_priority = lf_hovered((vec2s){lf_get_ptr_x(), lf_get_ptr_y()}, (vec2s){priority_size, priority_size}) && lf_mouse_button_went_down(GLFW_MOUSE_BUTTON_LEFT);

        if(clicked_priority){
            if(entry->priority + 1 >= PRIORITY_HIGH + 1){
                entry->priority = 0;
            } else {
                entry->priority++;
            }
            sort_entries_by_priority(entries);
        }
        switch (entry->priority)
        {
            case PRIORITY_LOW:{
                lf_rect(priority_size, priority_size, (LfColor){75, 175, 80, 255}, 4.0f);
                break;
            }
            case PRIORITY_MEDIUM:{
                lf_rect(priority_size, priority_size, (LfColor){255, 235, 59, 255}, 4.0f);
                break;
            }
            case PRIORITY_HIGH:{
                lf_rect(priority_size, priority_size, (LfColor){244, 67, 54, 255}, 4.0f);
                break;
            }
        }
        lf_set_ptr_y_absolute(ptry_before);

        {
            LfUIElementProps props = lf_get_theme().button_props;
            props.color = LF_NO_COLOR;
            props.border_width = 0.0f;
            props.padding = 0.0f;
            props.margin_top = 0.0f;
            props.margin_left = 10.0f;
            lf_push_style_props(props);
            if(lf_image_button(((LfTexture){.id = removeTexture.id, .width = 20, .height = 20})) == LF_CLICKED)
            {
                for(uint32_t j = i; j < numEntries - 1; j++)
                {
                    entries[j] = entries[j + 1];
                }
                numEntries--;
            }
            lf_pop_style_props();
        }

        {
            LfUIElementProps props = lf_get_theme().checkbox_props;
            props.border_width = 1.0f;
            props.corner_radius = 0.0f;
            props.margin_top = 0;
            props.padding = 5.0f;
            props.margin_left = 10.0f;
            props.color = lf_color_from_zto((vec4s){0.05f, 0.05f, 0.05f, 1.0f});
            lf_push_style_props(props);
            if(lf_checkbox("", &entry->completed, LF_NO_COLOR, ((LfColor){65, 167, 204, 255})) == LF_CLICKED){

            }
            lf_pop_style_props();
        }
        lf_push_font(&smallfont);
        LfUIElementProps props = lf_get_theme().text_props;
        props.margin_top = 0.0f;
        props.margin_left = 5.0f;
        lf_push_style_props(props);

        float descprt_x = lf_get_ptr_x();
        lf_text(entry->desc);

        lf_set_ptr_x_absolute(descprt_x);
        lf_set_ptr_y_absolute(lf_get_ptr_y() + smallfont.font_size);
        props.text_color = (LfColor){150, 150, 150, 255};
        lf_push_style_props(props);
        lf_text(entry->date);
        lf_pop_style_props();
        lf_pop_font();

        lf_next_line();
        
        renderedcount++;
    } 

    if(!renderedcount){
        lf_text("There is no task here.");
    }
    lf_div_end();
         
}

static void rendernewtask(){
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);

    lf_push_font(&titlefont);
    {
        LfUIElementProps props = lf_get_theme().text_props;
        props.margin_bottom = 15.0f;
        lf_push_style_props(props);
        lf_text("Add new task");
        lf_pop_font();
    }

    lf_next_line();

    {
        lf_push_font(&smallfont);
        lf_text("Description");
        lf_pop_font();

        lf_next_line();
        LfUIElementProps props = lf_get_theme().inputfield_props;
        props.padding = 15.0f;
        props.border_width = 1.0f;
        props.color = BACKGROUND_COLOR;
        props.corner_radius = 11;
        props.text_color = LF_WHITE;
        props.border_color = new_task_input.selected ? LF_WHITE : (LfColor){170, 170, 170, 255};
        props.corner_radius = 2.5f;
        props.margin_bottom = 10.0f;
        lf_push_style_props(props);
        lf_input_text(&new_task_input);
        lf_pop_style_props();
    }

    lf_next_line();

    
    {
        lf_push_font(&smallfont);
        lf_text("Priority");
        lf_pop_font();

        lf_next_line();
        static const char* items [3] = {
            "Low",
            "Medium",
            "High"
        };
        static bool opened = false;
        LfUIElementProps props = lf_get_theme().button_props;
        props.color = (LfColor){40, 40, 40, 255};
        props.text_color = LF_WHITE;
        props.border_width = 0.0f;
        props.corner_radius = 5.0f;
        lf_push_style_props(props);
        lf_dropdown_menu(items, "Priority", 3, 200, 80, &selected_priority, &opened);
        lf_pop_style_props();
    }

    {
        bool form_complete = (strlen(new_task_input_buf) && selected_priority != -1);
        const char* text = "Add";
        const float width = 150.0f;

        LfUIElementProps props = lf_get_theme().button_props;
        props.margin_left = 0.0f;
        props.margin_right = 0.0f;
        props.corner_radius = 5.0f;
        props.border_width = 0.0f;
        props.color = !form_complete ? (LfColor){80, 80, 80, 255} : (LfColor){65, 167, 204, 255};
        lf_push_style_props(props);
        lf_set_line_should_overflow(false);
        lf_set_ptr_x_absolute(WIN_INIT_W - (width + props.padding * 2.0f) - GLOBAL_MARGIN);
        lf_set_ptr_y_absolute(WIN_INIT_H - (lf_button_dimension(text).y + props.padding * 2.0f) - GLOBAL_MARGIN);
        if(lf_button_fixed(text, width, -1) == LF_CLICKED || lf_key_went_down(GLFW_KEY_ENTER) && form_complete)
        {
            todo_entry* entry = (todo_entry*)malloc(sizeof(*entry));
            entry->priority = selected_priority;
            entry->completed = false;
            entry->date = get_command_output("date +\"%d.%m.%Y, %H:%M\"");

            char* new_desc = malloc(strlen(new_task_input_buf));
            strcpy(new_desc, new_task_input_buf);

            entry->desc = new_desc;  
            entries[numEntries++] = entry;
            memset(new_task_input_buf, 0, 512);
            new_task_input.cursor_index = 0;
            lf_input_field_unselect_all(&new_task_input);
            sort_entries_by_priority(entries);
        }
        lf_set_line_should_overflow(true);
        lf_pop_style_props();
    }

    lf_next_line();

    {
        LfUIElementProps props = lf_get_theme().button_props;
        props.color = LF_NO_COLOR;
        props.border_width = 0.0f;
        props.padding = 0.0f;
        props.margin_left = 0.0f;
        props.margin_top = 0.0f;
        props.margin_right = 0.0f;
        props.margin_bottom = 0.0f;
        lf_push_style_props(props);
        lf_set_line_should_overflow(false);
        LfTexture backButton = (LfTexture){.id = backTexture.id, .width = 20, .height = 40};
        lf_set_ptr_y_absolute(WIN_INIT_H - backButton.height - GLOBAL_MARGIN * 2.0f);
        lf_set_ptr_x_absolute(GLOBAL_MARGIN);

        if(lf_image_button(backButton) == LF_CLICKED){
            current_tab = TAB_DASHBOARD;
        }
        lf_set_line_should_overflow(true);
        lf_pop_style_props();
    }
}

int main(int argc, char **argv) 
{
    load_entries_from_json("todo_tasks.json");
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(WIN_INIT_W, WIN_INIT_H, "Todo", NULL, NULL);

    glfwMakeContextCurrent(window);

    lf_init_glfw(WIN_INIT_W, WIN_INIT_H, window);
    titlefont = lf_load_font("./fonts/inter-bold.ttf", 40);
    smallfont = lf_load_font("./fonts/inter.ttf", 20);

    removeTexture = lf_load_texture("./icons/remove.png", true, LF_TEX_FILTER_LINEAR);
    backTexture = lf_load_texture("./icons/back.png", true, LF_TEX_FILTER_LINEAR);

    memset(new_task_input_buf, 0, INPUT_BUF_SIZE);
    new_task_input = (LfInputField){
        .width = 400,
        .buf = new_task_input_buf,
        .buf_size = INPUT_BUF_SIZE,
        .placeholder = "What is there to do?"
    };


    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);

        lf_begin();
        
        lf_div_begin(((vec2s){GLOBAL_MARGIN, GLOBAL_MARGIN}), ((vec2s){WIN_INIT_W - GLOBAL_MARGIN * 2.0f, WIN_INIT_H - GLOBAL_MARGIN * 2.0f}), true);

        switch(current_tab){
            case TAB_DASHBOARD:{
                rendertopbar();
                lf_next_line();
                renderfilters();
                lf_next_line();
                renderentries();
                break;
            }
            
            case TAB_NEW_TASK:{
                rendernewtask();
            }

        }

        lf_div_end();
        lf_end();

        glfwPollEvents();
        glfwSwapBuffers(window);
    }

    save_entries_to_json("todo_tasks.json");

    lf_free_font(&titlefont);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}