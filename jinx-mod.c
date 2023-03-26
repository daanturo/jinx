/* Jinx bindings to libenchant

Copyright (C) 2023 Free Software Foundation, Inc.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

#include <emacs-module.h>
#include <enchant.h>
#include <string.h>
#include <glib.h>

int plugin_is_GPL_compatible;

static EnchantBroker* broker = 0;

#define jinx_unused(var) _##var __attribute__((unused))

static emacs_value jinx_str(emacs_env* env, const char* str) {
    return env->make_string(env, str, strlen(str));
}

static emacs_value jinx_cons(emacs_env* env, emacs_value a, emacs_value b) {
    return env->funcall(env, env->intern(env, "cons"), 2, (emacs_value[]){a, b});
}

static char* jinx_cstr(emacs_env* env, emacs_value val) {
    ptrdiff_t size = 0;
    if (!env->copy_string_contents(env, val, 0, &size))
        return 0;
    char* str = malloc(size);
    if (!str) {
        env->non_local_exit_signal(env,
                                   env->intern (env, "memory-full"),
                                   env->intern (env, "nil"));
        return 0;
    }
    if (!env->copy_string_contents(env, val, str, &size)) {
        free(str);
        return 0;
    }
    return str;
}

static void jinx_defun(emacs_env* env, const char* name,
                       ptrdiff_t min, ptrdiff_t max, void* fun) {
    env->funcall(env, env->intern(env, "defalias"), 2,
                 (emacs_value[]){
                     env->intern(env, name),
                     env->make_function(env, min, max, fun, 0, 0)
                 });
}

static void jinx_free_dict(void* dict) {
    enchant_broker_free_dict(broker, dict);
}

static emacs_value jinx_dict(emacs_env* env, ptrdiff_t jinx_unused(nargs),
                             emacs_value args[], void* jinx_unused(data)) {
    if (!broker)
        broker = enchant_broker_init();
    g_autofree char* str = jinx_cstr(env, args[0]);
    EnchantDict* dict = str ? enchant_broker_request_dict(broker, str) : 0;
    return dict
        ? env->make_user_ptr(env, jinx_free_dict, dict)
        : env->intern(env, "nil");
}

static void jinx_describe_cb(const char* const lang_tag,
                             const char* const provider_name,
                             const char* const jinx_unused(provider_desc),
                             const char* const jinx_unused(provider_file),
                             void* data) {
    emacs_env* env = ((emacs_env**)data)[0];
    ((emacs_value*)data)[1] = jinx_cons(env,
                                        jinx_str(env, lang_tag),
                                        jinx_str(env, provider_name));
}

static emacs_value jinx_describe(emacs_env* env, ptrdiff_t jinx_unused(nargs),
                                 emacs_value args[], void* jinx_unused(data)) {
    EnchantDict* dict = env->get_user_ptr(env, args[0]);
    if (!dict)
        return env->intern(env, "nil");
    void* data[] = { env, 0 };
    enchant_dict_describe(dict, jinx_describe_cb, data);
    return data[1];
}

static emacs_value jinx_check(emacs_env* env, ptrdiff_t jinx_unused(nargs),
                              emacs_value args[], void* jinx_unused(data)) {
    g_autofree char* str = jinx_cstr(env, args[1]);
    EnchantDict* dict = env->get_user_ptr(env, args[0]);
    return env->intern(env, !str || !dict || enchant_dict_check(dict, str, -1)
                       ? "nil" : "t");
}

static emacs_value jinx_add(emacs_env* env, ptrdiff_t jinx_unused(nargs),
                            emacs_value args[], void* jinx_unused(data)) {
    g_autofree char* str = jinx_cstr(env, args[1]);
    EnchantDict* dict = env->get_user_ptr(env, args[0]);
    if (str && dict)
        enchant_dict_add(dict, str, -1);
    return env->intern(env, "nil");
}

static emacs_value jinx_wordchars(emacs_env* env, ptrdiff_t jinx_unused(nargs),
                                  emacs_value args[], void* jinx_unused(data)) {
    EnchantDict* dict = env->get_user_ptr(env, args[0]);
    return dict
        ? jinx_str(env, enchant_dict_get_extra_word_characters(dict))
        : env->intern(env, "nil");
}

static emacs_value jinx_suggest(emacs_env* env, ptrdiff_t jinx_unused(nargs),
                                emacs_value args[], void* jinx_unused(data)) {
    g_autofree char* str = jinx_cstr(env, args[1]);
    EnchantDict* dict = env->get_user_ptr(env, args[0]);
    emacs_value list = env->intern(env, "nil");
    size_t count = 0;
    char** suggs = str && dict ? enchant_dict_suggest(dict, str, -1, &count) : 0;
    if (suggs) {
        while (count > 0) {
            char* sugg = suggs[--count];
            list = jinx_cons(env, jinx_str(env, sugg), list);
        }
        enchant_dict_free_string_list(dict, suggs);
    }
    return list;
}

int emacs_module_init(struct emacs_runtime *runtime) {
    if (runtime->size < (ptrdiff_t)sizeof (*runtime))
        return 1;
    emacs_env* env = runtime->get_environment(runtime);
    jinx_defun(env, "jinx--mod-suggest", 2, 2, jinx_suggest);
    jinx_defun(env, "jinx--mod-check", 2, 2, jinx_check);
    jinx_defun(env, "jinx--mod-add", 2, 2, jinx_add);
    jinx_defun(env, "jinx--mod-dict", 1, 1, jinx_dict);
    jinx_defun(env, "jinx--mod-describe", 1, 1, jinx_describe);
    jinx_defun(env, "jinx--mod-wordchars", 1, 1, jinx_wordchars);
    return 0;
}