// Shell completion script generation for `odin completion <shell>`.
//
// Two sources feed into the generated script, each used for what it is actually authoritative about:
//
//   * The build flag table decides *which* flags a command accepts. It is what the parser itself
//     consults, so a completed flag is always one that parses.
//   * The `-help` text decides what those flags are *described* as, and which *values* the ones with
//     a fixed set of choices accept. Neither is recorded anywhere else in a machine-readable form.
//
// Nothing here restates either of them, so a generated script cannot drift away from the compiler
// that generated it.

struct CompletionValue {
	String name;
	String desc;
};

struct CompletionFlag {
	String                 name;        // Without the leading '-', e.g. "thread-count".
	String                 desc;        // First line of the flag's help text, empty if undocumented.
	bool                   takes_param;
	Array<CompletionValue> values;      // The accepted values, when the help text enumerates them.
};

struct CompletionCommand {
	String name;
	String desc;
};


gb_internal String completion_trim_spaces(String s) {
	while (s.len > 0 && (s[0] == ' ' || s[0] == '\t')) {
		s = substring(s, 1, s.len);
	}
	while (s.len > 0 && (s[s.len-1] == ' ' || s[s.len-1] == '\t')) {
		s = substring(s, 0, s.len-1);
	}
	return s;
}

// Prints a fish single-quoted string. Within those, only ' and \ carry meaning.
gb_internal void completion_print_fish_quoted(String s) {
	gb_printf("'");
	for (isize i = 0; i < s.len; i++) {
		if (s[i] == '\'' || s[i] == '\\') {
			gb_printf("\\");
		}
		gb_printf("%c", cast(char)s[i]);
	}
	gb_printf("'");
}

// Maps a command name back to the bit `command_support` is tested against.
gb_internal CommandKind completion_command_kind(String name) {
	for (isize i = 0; i < gb_count_of(odin_command_strings); i++) {
		char const *s = odin_command_strings[i];
		if (s == nullptr || *s == 0) {
			continue;
		}
		if (name == make_string_c(s)) {
			return cast(CommandKind)(cast(u64)1 << i);
		}
	}
	// `bundle` dispatches on a platform argument; Android is the only one implemented.
	if (name == "bundle") {
		return Command_bundle_android;
	}
	return cast(CommandKind)0;
}

// Only these headers introduce an exhaustive list of accepted values. Others, such as `Examples:`
// and `Usage in code:`, are illustrative -- `-target:linux_amd64` is one target out of dozens -- and
// completing from them would offer values that happen not to exist.
gb_internal bool completion_is_value_header(String header) {
	return str_eq_ignore_case(header, str_lit("Available options:")) ||
	       str_eq_ignore_case(header, str_lit("Available subtargets:")) ||
	       str_eq_ignore_case(header, str_lit("Choices:"));
}

// Values are spelled either `-flag:value   Description` or, under a `Choices:` header, bare as
// `value   Description`. Returns false for anything that does not look like a single literal value.
gb_internal bool completion_parse_value(String line, String flag_name, CompletionValue *value_) {
	String rest = completion_trim_spaces(line);

	// Strip the `-flag:` prefix that most, but not all, of the help text repeats.
	if (rest.len > flag_name.len+2 && rest[0] == '-' &&
	    substring(rest, 1, flag_name.len+1) == flag_name && rest[flag_name.len+1] == ':') {
		rest = substring(rest, flag_name.len+2, rest.len);
	}

	isize end = 0;
	for (; end < rest.len; end++) {
		if (rest[end] == ' ' || rest[end] == '\t') {
			break;
		}
	}

	String name = substring(rest, 0, end);
	if (name.len == 0) {
		return false;
	}
	for (isize i = 0; i < name.len; i++) {
		u8 c = name[i];
		bool const ok = gb_char_is_alphanumeric(cast(char)c) || c == '-' || c == '_' || c == '.';
		if (!ok) {
			return false;
		}
	}

	CompletionValue value = {};
	value.name = name;
	value.desc = completion_trim_spaces(substring(rest, end, rest.len));
	*value_ = value;
	return true;
}

// Reads `<arg0> <command> -help` back out of itself. Flags are printed at indent 1 and are the only
// lines there that begin with a '-'; the description follows at indent 2, and any enumerated values
// at indent 3 beneath a header such as `Available options:`.
gb_internal Array<CompletionFlag> completion_parse_help(String const arg0, String command) {
	auto lines = array_make<UsageLine>(heap_allocator(), 0, 512);
	defer (array_free(&lines));

	usage_line_sink = &lines;
	print_show_help(arg0, command);
	usage_line_sink = nullptr;

	auto documented = array_make<CompletionFlag>(heap_allocator(), 0, 128);

	isize current = -1;
	bool under_value_header = false;

	for (UsageLine const &line : lines) {
		if (line.indent <= 1) {
			current = -1;
			under_value_header = false;

			if (line.indent != 1 || line.text.len < 2 || line.text[0] != '-') {
				continue;
			}

			CompletionFlag flag = {};
			flag.name = substring(line.text, 1, line.text.len);

			isize sep = string_index_byte(flag.name, ':');
			if (sep >= 0) {
				flag.name = substring(flag.name, 0, sep);
			}
			// NOTE: Held in the permanent allocator because the parsed values outlive this array.
			flag.values = array_make<CompletionValue>(permanent_allocator(), 0, 8);

			array_add(&documented, flag);
			current = documented.count-1;
			continue;
		}

		if (current < 0) {
			continue;
		}

		if (line.indent == 2) {
			// The first line under a flag is its description; later ones may open a value list.
			if (documented[current].desc.len == 0 && !completion_is_value_header(line.text)) {
				documented[current].desc = line.text;
			}
			under_value_header = completion_is_value_header(line.text);
			continue;
		}

		if (under_value_header) {
			CompletionValue value = {};
			if (completion_parse_value(line.text, documented[current].name, &value)) {
				array_add(&documented[current].values, value);
			}
		}
	}

	return documented;
}

gb_internal CompletionFlag const *completion_find_flag(Array<CompletionFlag> const &flags, String name) {
	for (CompletionFlag const &flag : flags) {
		if (flag.name == name) {
			return &flag;
		}
	}
	return nullptr;
}

// A few flags are registered against more commands than they actually work with, so that using one
// in the wrong place reports what is really wrong instead of just "unknown flag". Their handlers in
// `parse_build_flags` then reject them. Completing those would only ever offer a guaranteed error.
gb_internal bool completion_flag_rejected_by_command(String flag_name, String command) {
	// See the `BuildFlag_BuildMode` case: "'build-mode' can only be used with the 'build' command".
	if (flag_name == "build-mode") {
		return command != "build";
	}
	return false;
}

// The flags `command` actually accepts, described using the help text wherever it has something to
// say about them. `fallback` is the help for every command at once, consulted for the flags that a
// command accepts but does not document -- `-build-mode` is spelled out under `build` alone, yet
// `run` and `test` take it just the same.
gb_internal Array<CompletionFlag> completion_collect_flags(String const arg0, String command, Array<CompletionFlag> const &fallback) {
	CommandKind const kind = completion_command_kind(command);

	auto flags = array_make<CompletionFlag>(heap_allocator(), 0, BuildFlag_COUNT);

	// Only the commands that reach `parse_build_flags` take build flags at all; the rest bail out of
	// `main` before it runs. Every command does understand `-help`, though.
	if ((kind & (Command__does_check|Command_bundle_android)) == 0) {
		CompletionFlag help_flag = {};
		help_flag.name = str_lit("help");
		help_flag.desc = str_lit("Shows this help text.");
		array_add(&flags, help_flag);
		return flags;
	}

	auto documented = completion_parse_help(arg0, command);
	defer (array_free(&documented));

	auto build_flags = make_build_flags();
	defer (array_free(&build_flags));

	for (BuildFlag const &bf : build_flags) {
		if ((bf.command_support & kind) == 0) {
			continue;
		}
		// Flags kept for the compiler's own development are deliberately left undocumented.
		if (string_starts_with(bf.name, str_lit("internal-"))) {
			continue;
		}
		if (completion_flag_rejected_by_command(bf.name, command)) {
			continue;
		}

		CompletionFlag flag = {};
		flag.name = bf.name;
		flag.takes_param = bf.param_kind != BuildFlagParam_None;

		CompletionFlag const *primary = completion_find_flag(documented, bf.name);
		CompletionFlag const *backup  = completion_find_flag(fallback, bf.name);

		if (primary != nullptr && primary->desc.len > 0) {
			flag.desc = primary->desc;
		} else if (backup != nullptr) {
			flag.desc = backup->desc;
		}

		if (primary != nullptr && primary->values.count > 0) {
			flag.values = primary->values;
		} else if (backup != nullptr) {
			flag.values = backup->values;
		}

		array_add(&flags, flag);
	}

	return flags;
}

// A few flags accept far too many values to spell out one completion each, and `-microarch` and
// -target-features` accept a different set per target architecture. `-flag:"?"` prints those lists;
// the tables behind it are read directly here, and the architecture is resolved in the shell so that
// a `-target=` already on the command line is respected.
gb_internal char const *completion_fish_value_source(String flag_name) {
	if (flag_name == "target") {
		return "__fish_odin_prefixed -target= $__fish_odin_targets";
	}
	if (flag_name == "microarch") {
		// `native` lives outside the table: the handler resolves it to the host CPU at runtime,
		// so the compiler does not list it in `-microarch:"?"`. Prepend it via a dedicated helper.
		return "__fish_odin_microarch_values";
	}
	if (flag_name == "target-features") {
		return "__fish_odin_arch_values -target-features= features";
	}
	return nullptr;
}

// Prints `set -g <name> v1 v2 ...` from one of the comma separated tables. Anything that is not a
// plain literal is dropped rather than quoted, since a value needing quoting is not a value.
gb_internal void completion_print_fish_set(char const *name, String arch_name, String comma_separated) {
	if (comma_separated.len == 0) {
		return;
	}

	gb_printf("set -g %s%.*s", name, LIT(arch_name));

	String_Iterator it = {comma_separated, 0};
	String value = {};
	while (string_split_iterator_next(&it, ',', &value)) {
		value = completion_trim_spaces(value);
		if (value.len == 0) {
			continue;
		}

		bool literal = true;
		for (isize i = 0; i < value.len; i++) {
			char const c = cast(char)value[i];
			if (!gb_char_is_alphanumeric(c) && c != '-' && c != '_' && c != '.' && c != '+') {
				literal = false;
				break;
			}
		}
		if (literal) {
			gb_printf(" %.*s", LIT(value));
		}
	}

	gb_printf("\n");
}

gb_internal void completion_print_fish_value_lists(void) {
	String const default_arch = target_arch_names[default_target_metrics()->arch];

	gb_printf("\n# ---- values of `-target`, `-microarch` and `-target-features` ----\n");
	gb_printf("# The latter two differ per architecture, so the list is picked from any `-target=` on\n");
	gb_printf("# the command line, falling back to the one this compiler builds for by default.\n");

	gb_printf("set -g __fish_odin_default_arch %.*s\n", LIT(default_arch));

	gb_printf("set -g __fish_odin_targets");
	for (isize i = 0; i < gb_count_of(named_targets); i++) {
		gb_printf(" %.*s", LIT(named_targets[i].name));
	}
	gb_printf("\n");

	for (isize arch = 0; arch < TargetArch_COUNT; arch++) {
		if (arch == TargetArch_Invalid) {
			continue;
		}
		completion_print_fish_set("__fish_odin_microarch_", target_arch_names[arch], target_microarch_list[arch]);
		completion_print_fish_set("__fish_odin_features_",  target_arch_names[arch], target_features_list[arch]);
	}

	gb_printf("\n");
	gb_printf("function __fish_odin_prefixed -d 'Prefix every remaining argument with the first'\n");
	// NOTE: fish's `printf` takes `--` as the format string rather than as an end-of-options marker,
	// and cycles its format over the arguments, so the prefix is concatenated onto the list instead.
	gb_printf("    printf '%%s\\n' $argv[1]$argv[2..]\n");
	gb_printf("end\n");
	gb_printf("\n");
	gb_printf("function __fish_odin_target_arch -d 'Architecture a target name builds for'\n");
	gb_printf("    switch $argv[1]\n");
	for (isize arch = 0; arch < TargetArch_COUNT; arch++) {
		if (arch == TargetArch_Invalid) {
			continue;
		}

		isize matches = 0;
		for (isize i = 0; i < gb_count_of(named_targets); i++) {
			if (named_targets[i].metrics->arch != arch) {
				continue;
			}
			if (matches++ == 0) {
				gb_printf("        case");
			}
			gb_printf(" %.*s", LIT(named_targets[i].name));
		}
		if (matches > 0) {
			gb_printf("\n            echo %.*s\n", LIT(target_arch_names[arch]));
		}
	}
	gb_printf("        case '*'\n");
	gb_printf("            echo $__fish_odin_default_arch\n");
	gb_printf("    end\n");
	gb_printf("end\n");
	gb_printf("\n");
	gb_printf("function __fish_odin_arch -d 'Architecture the current command line targets'\n");
	gb_printf("    for tok in (commandline -pxc)\n");
	gb_printf("        if string match -qr -- '^-{1,2}target[:=]' $tok\n");
	gb_printf("            __fish_odin_target_arch (string replace -r -- '^-{1,2}target[:=]' '' $tok)\n");
	gb_printf("            return\n");
	gb_printf("        end\n");
	gb_printf("    end\n");
	gb_printf("    echo $__fish_odin_default_arch\n");
	gb_printf("end\n");
	gb_printf("\n");
	gb_printf("function __fish_odin_arch_values -d 'Values an architecture-dependent flag accepts'\n");
	gb_printf("    set -l var __fish_odin_$argv[2]_(__fish_odin_arch)\n");
	gb_printf("    set -q $var; or return\n");
	gb_printf("    printf '%%s\\n' $argv[1]$$var\n");
	gb_printf("end\n");
	gb_printf("\n");
	gb_printf("function __fish_odin_microarch_values -d 'Values of `-microarch`, plus `native`'\n");
	gb_printf("    printf '%%s\\n' -microarch=native\n");
	gb_printf("    __fish_odin_arch_values -microarch= microarch\n");
	gb_printf("end\n");
}

gb_internal void completion_print_fish_flags_for(String const arg0, String command, Array<CompletionFlag> const &fallback) {
	auto flags = completion_collect_flags(arg0, command, fallback);
	defer (array_free(&flags));

	if (flags.count == 0) {
		return;
	}

	gb_printf("\n# ---- flags for `%.*s` ----\n", LIT(command));
	for (CompletionFlag const &flag : flags) {
		gb_printf("complete -c odin -n '__fish_seen_subcommand_from %.*s' -a '-%.*s%s'",
		          LIT(command), LIT(flag.name), flag.takes_param ? "=" : "");
		if (flag.desc.len > 0) {
			gb_printf(" -d ");
			completion_print_fish_quoted(flag.desc);
		}
		gb_printf("\n");

		// The values are held back until the flag itself has been typed, so that listing the flags
		// does not bury them under every value each one accepts.
		char const *value_source = completion_fish_value_source(flag.name);
		if (value_source != nullptr) {
			gb_printf("complete -c odin -n '__fish_seen_subcommand_from %.*s; and __fish_odin_completing_value -%.*s=' -a '(%s)'\n",
			          LIT(command), LIT(flag.name), value_source);
			continue;
		}

		for (CompletionValue const &value : flag.values) {
			gb_printf("complete -c odin -n '__fish_seen_subcommand_from %.*s; and __fish_odin_completing_value -%.*s=' -a '-%.*s=%.*s'",
			          LIT(command), LIT(flag.name), LIT(flag.name), LIT(value.name));
			if (value.desc.len > 0) {
				gb_printf(" -d ");
				completion_print_fish_quoted(value.desc);
			}
			gb_printf("\n");
		}
	}
}

// Collects the commands out of the `Commands:` section of the top level usage text.
gb_internal Array<CompletionCommand> completion_collect_commands(String const arg0) {
	auto lines = array_make<UsageLine>(heap_allocator(), 0, 64);
	defer (array_free(&lines));

	usage_line_sink = &lines;
	usage(arg0);
	usage_line_sink = nullptr;

	auto commands = array_make<CompletionCommand>(heap_allocator(), 0, lines.count);

	bool in_command_section = false;
	for (UsageLine const &line : lines) {
		if (line.indent == 0) {
			// The section ends at whatever indent 0 line follows it.
			if (in_command_section) {
				break;
			}
			in_command_section = line.text == "Commands:";
			continue;
		}
		// A line starting with a space continues the description of the command above it.
		if (!in_command_section || line.indent != 1 || line.text.len == 0 || line.text[0] == ' ') {
			continue;
		}

		isize split = string_index_byte(line.text, ' ');
		if (split < 0) {
			continue;
		}

		CompletionCommand cmd = {};
		cmd.name = substring(line.text, 0, split);
		cmd.desc = completion_trim_spaces(substring(line.text, split, line.text.len));
		array_add(&commands, cmd);
	}

	return commands;
}

gb_internal int generate_fish_completion(void) {
	// NOTE: The help text is captured as if it had been invoked as plain `odin`, since that is the
	// name the generated script completes for, rather than whatever path this binary was run from.
	String const arg0 = str_lit("odin");

	auto commands = completion_collect_commands(arg0);
	defer (array_free(&commands));

	// `odin help` enables every flag category at once, so it doubles as a description of record for
	// the flags an individual command accepts without documenting.
	auto all_flags_help = completion_parse_help(arg0, str_lit("help"));
	defer (array_free(&all_flags_help));

	gb_printf("# Fish shell completions for the Odin compiler.\n");
	gb_printf("#\n");
	gb_printf("# Generated by `odin completion fish` from this compiler's own flag table and help text.\n");
	gb_printf("# Regenerate it after updating Odin so that the two stay in step:\n");
	gb_printf("#\n");
	gb_printf("#     odin completion fish > ~/.config/fish/completions/odin.fish\n");
	gb_printf("\n");
	gb_printf("# Odin spells its flags `-flag:value`, but also accepts `-flag=value`. The generated\n");
	gb_printf("# completions use the latter, because fish leaves the cursor right after a '=' instead\n");
	gb_printf("# of inserting a space, letting the value be typed straight away.\n");
	gb_printf("\n");
	gb_printf("function __fish_odin_completing_value -d 'Is the token being completed a value for this flag?'\n");
	gb_printf("    string match -q -- \"$argv[1]*\" (commandline -ct)\n");
	gb_printf("end\n");
	gb_printf("\n");
	gb_printf("complete -c odin -f\n");
	gb_printf("\n");
	gb_printf("# ---- commands ----\n");

	for (CompletionCommand const &cmd : commands) {
		gb_printf("complete -c odin -n __fish_use_subcommand -a '%.*s' -d ", LIT(cmd.name));
		completion_print_fish_quoted(cmd.desc);
		gb_printf("\n");
	}
	// `help` is described in prose rather than listed in the usage text, so it is spelled out here.
	gb_printf("complete -c odin -n __fish_use_subcommand -a 'help' -d 'Shows the help for a command.'\n");

	gb_printf("\n# ---- package directory or .odin file ----\n");
	gb_printf("complete -c odin -n '__fish_seen_subcommand_from build run check test doc strip-semicolon' -k -a '(__fish_complete_suffix .odin)'\n");

	gb_printf("\n# ---- arguments the executable is run with ----\n");
	gb_printf("complete -c odin -n '__fish_seen_subcommand_from run test' -a '--' -d 'Pass the remaining arguments to the compiled executable'\n");

	gb_printf("\n# ---- bundle platforms ----\n");
	gb_printf("complete -c odin -n '__fish_seen_subcommand_from bundle' -a 'android' -d 'Bundle for Android'\n");

	gb_printf("\n# ---- shells `odin completion` can generate for ----\n");
	gb_printf("complete -c odin -n '__fish_seen_subcommand_from completion' -a 'fish' -d 'Fish shell'\n");

	gb_printf("\n# ---- help topics ----\n");
	gb_printf("complete -c odin -n '__fish_seen_subcommand_from help' -a '");
	for_array(i, commands) {
		gb_printf("%s%.*s", i == 0 ? "" : " ", LIT(commands[i].name));
	}
	gb_printf("' -d 'Command to show help for'\n");

	completion_print_fish_value_lists();

	for (CompletionCommand const &cmd : commands) {
		completion_print_fish_flags_for(arg0, cmd.name, all_flags_help);
	}

	return 0;
}

gb_internal int generate_shell_completion(String const arg0, String shell) {
	if (shell == "fish") {
		return generate_fish_completion();
	}

	if (shell.len == 0) {
		gb_printf_err("ERROR: `%.*s completion` requires a shell to generate completions for.\n", LIT(arg0));
	} else {
		gb_printf_err("ERROR: unsupported shell '%.*s'.\n", LIT(shell));
	}
	gb_printf_err("Supported shells: fish\n");
	gb_printf_err("Example: %.*s completion fish > ~/.config/fish/completions/odin.fish\n", LIT(arg0));
	return 1;
}
