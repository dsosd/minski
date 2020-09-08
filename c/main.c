#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long Value;

enum Token_t{
	TOKEN_NULL,
	TOKEN_S,
	TOKEN_K,
	TOKEN_I,
	TOKEN_WRAP,
	TOKEN_LAZY_WRAP,
	TOKEN_FUNC,
	TOKEN_VAL
};

struct Token{
	enum Token_t type;
	unsigned long id;
	Value val;

	void* data;
	size_t len;

	int ref_count;
};

struct Token gen_token(enum Token_t type, unsigned long* curr_id){
	struct Token ret;

	ret.type = type;
	ret.id = ++*curr_id;

	ret.data = NULL;
	ret.len = 0;

	ret.ref_count = -1;
	return ret;
}

struct Token* new_token(enum Token_t type, unsigned long* curr_id){
	struct Token* ret = malloc(sizeof(struct Token));

	if (ret){
		*ret = gen_token(type, curr_id);
		ret->ref_count = 1;

		if (type == TOKEN_WRAP){
			ret->data = malloc(2 * sizeof(void*));
			ret->len = 2;
		}
	}
	return ret;
}

void free_token(struct Token* token){
	if (!token){
		return;
	}

	--token->ref_count;
	if (!token->ref_count){
		if (token->type == TOKEN_WRAP || token->type == TOKEN_LAZY_WRAP){
			if (token->type == TOKEN_WRAP){
				for (int i = 0; i < token->len; ++i){
					free_token(((void**)token->data)[i]);
				}
			}
			free(token->data);
		}
		free(token);
	}
}

struct Rbuf{
	unsigned long capacity;

	unsigned long offset;
	unsigned long size;

	void* data;
};

void* rbuf_get_ptr(const struct Rbuf* rbuf, int index){
	return ((void**)rbuf->data) [(rbuf->offset + index) % rbuf->capacity];
}

void rbuf_set_ptr(struct Rbuf* rbuf, int index, void* ptr){
	((void**)rbuf->data) [(rbuf->offset + index) % rbuf->capacity] = ptr;
}

struct Vec{
	unsigned long capacity;
	unsigned long size;

	void* data;
};

void* vec_get_ptr(const struct Vec* vec, int index){
	return ((void**)vec->data)[index];
}

void vec_push_back_ptr(struct Vec* vec, void* val){
	//double capacity if lacking space (min capacity = 10)
	if (vec->size + 1 > vec->capacity){
		unsigned long new_capacity = (vec->size < 5) ? 10 : 2 * vec->capacity;
		void** new_data = malloc(new_capacity * sizeof(void*));

		if (vec->size){
			memcpy(new_data, vec->data, vec->size * sizeof(void*));
			free(vec->data);
		}
		vec->data = new_data;
		vec->capacity = new_capacity;

	}

	((void**)vec->data)[vec->size++] = val;
}

bool rbuf_advance(struct Rbuf* rbuf, int n){
	if (n > rbuf->size){
		return false;
	}

	rbuf->offset += n;
	rbuf->offset %= rbuf->capacity;
	rbuf->size -= n;

	return true;
}

void rbuf_resize_ptr(struct Rbuf* rbuf, unsigned long min_size){
	if (rbuf->capacity >= min_size){
		return;
	}

	//MAGIC double capacity at least
	min_size = (min_size > 2 * rbuf->capacity) ? min_size : 2 * rbuf->capacity;
	void* new_data = malloc(min_size * sizeof(void*));

	//[ second_len ... empty_space ... first_len ]
	unsigned long first_len = rbuf->capacity - rbuf->offset;
	first_len = (first_len < rbuf->size) ? first_len : rbuf->size;
	unsigned long second_len = rbuf->size - first_len;
	memcpy(new_data, (char*)rbuf->data + rbuf->offset * sizeof(void*), first_len * sizeof(void*));
	memcpy((char*)new_data + first_len * sizeof(void*), rbuf->data, second_len * sizeof(void*));

	free(rbuf->data);
	rbuf->data = new_data;
	rbuf->capacity = min_size;
	rbuf->offset = 0;
}

Value successor(Value val){
	return val + 1;
}

Value input(Value val){
	char ret;
	int input_char = fgetc(stdin);
	if (input_char == EOF){
		ret = '\0';
	}

	ret = input_char;
	return ret;
}

Value output(Value val){
	return fputc(val, stdout) != EOF;
}

void print_token_buf(struct Rbuf* tokens){
	for (int i = 0; i < tokens->size; ++i){
		struct Token* token = rbuf_get_ptr(tokens, i);

		switch (token->type){
		case TOKEN_S:
			printf("S");
			break;
		case TOKEN_K:
			printf("K");
			break;
		case TOKEN_I:
			printf("I");
			break;
		case TOKEN_WRAP:
			printf("(...)");
			break;
		case TOKEN_LAZY_WRAP:
			printf("(=)");
			break;
		case TOKEN_FUNC:
			printf("f");
			break;
		case TOKEN_VAL:
			printf("v");
			break;
		case TOKEN_NULL:
		default:
			printf("?");
		}
	}
}

bool lazy_unwrap(struct Rbuf* tokens, unsigned long* curr_token_id, int index){
	struct Token* token = rbuf_get_ptr(tokens, index);

	//DEBUG/printf("Lazy unwrapping ");

	struct Vec wrapped_tokens = {0, 0, NULL};
	int paren_depth = 0;
	int paren_pos = 0;

	int i_begin = 0;
	int i_end = token->len;
	//strip the outermost set of parentheses if it encloses the entire string
	bool balanced_parens = token->len >= 2
		&& ((char*)token->data)[i_begin] == '('
		&& ((char*)token->data)[i_end - 1] == ')'
	;

	bool closed_out = false;
	for (int i = i_begin; i < i_end; ++i){
		char symbol = ((char*)token->data)[i];
		if (symbol == '('){
			if (closed_out){
				balanced_parens = false;
			}
			++paren_depth;
		}
		else if (symbol == ')'){
			--paren_depth;
			if (!paren_depth){
				closed_out = true;
			}
		}

		if (paren_depth < 0){
			balanced_parens = false;
		}
	}
	balanced_parens &= !paren_depth;
	paren_depth = 0;

	if (balanced_parens){
		++i_begin;
		--i_end;
	}

	for (int i = i_begin; i < i_end; ++i){
		char symbol = ((char*)token->data)[i];
		//DEBUG/printf("%c", symbol);

		if (paren_depth){
			if (symbol == '('){
				++paren_depth;
			}
			else if (symbol == ')'){
				--paren_depth;
			}
			//unbalanced parentheses
			if (paren_depth < 0){
				return false;
			}

			if (!paren_depth){
				struct Token* new_lazy = new_token(TOKEN_LAZY_WRAP, curr_token_id);
				new_lazy->len = i - paren_pos + 1;
				new_lazy->data = malloc(new_lazy->len);
				memcpy(new_lazy->data, (char*)token->data + paren_pos, new_lazy->len);

				vec_push_back_ptr(&wrapped_tokens, new_lazy);
			}
		}
		else{
			if (symbol == 'S'){
				vec_push_back_ptr(&wrapped_tokens, new_token(TOKEN_S, curr_token_id));
			}
			else if (symbol == 'K'){
				vec_push_back_ptr(&wrapped_tokens, new_token(TOKEN_K, curr_token_id));
			}
			else if (symbol == 'I'){
				vec_push_back_ptr(&wrapped_tokens, new_token(TOKEN_I, curr_token_id));
			}
			else if (symbol == '('){
				paren_pos = i;
				paren_depth = 1;
			}
			else{
				return false;
			}
		}
	}
	//DEBUG/printf("\n");

	if (paren_depth){
		return false;
	}

	struct Token* wrap_token = new_token(TOKEN_NULL, curr_token_id);
	wrap_token->type = TOKEN_WRAP;
	wrap_token->len = wrapped_tokens.size;
	wrap_token->data = malloc(wrap_token->len * sizeof(void*));
	memcpy(wrap_token->data, wrapped_tokens.data, wrap_token->len * sizeof(void*));

	rbuf_set_ptr(tokens, index, wrap_token);

	free(wrapped_tokens.data);
	free_token(token);
	return true;
}

int main(int argc, char** argv){
	//args: input_filename
	if (argc < 2){
		return 1;
	}

	FILE* in = fopen(argv[1], "r");
	if (!in){
		return 1;
	}
	//TODO somehow only do one pass without potential memory reallocation costs
	int file_size = 0;
	while (!feof(in)){
		fgetc(in);
		++file_size;
	}
	char* preset_str = malloc((file_size + 1) * sizeof(char));
	rewind(in);
	fgets(preset_str, file_size + 1, in);
	fclose(in);

	unsigned long curr_token_id = 0;
	//used to prevent runaway recursion or excessively long parse times
	unsigned long max_token_id = 12000;

	//MAGIC start off with 100 tokens capacity
	struct Rbuf tokens = {
		100,
		0,
		0,
		malloc(tokens.capacity * sizeof(void*))
	};

	struct Token* preset_token = new_token(TOKEN_LAZY_WRAP, &curr_token_id);
	preset_token->len = strlen(preset_str);
	preset_token->data = malloc(preset_token->len * sizeof(char));
	memcpy(preset_token->data, preset_str, preset_token->len);
	free(preset_str);

	rbuf_set_ptr(&tokens, 0, preset_token);
	++tokens.size;

	struct Token* zero_token = new_token(TOKEN_VAL, &curr_token_id);
	zero_token->val = 0;
	rbuf_set_ptr(&tokens, tokens.size++, zero_token);
	struct Token* succ_token = new_token(TOKEN_FUNC, &curr_token_id);
	succ_token->data = &successor;
	rbuf_set_ptr(&tokens, tokens.size++, succ_token);
	struct Token* output_token = new_token(TOKEN_FUNC, &curr_token_id);
	output_token->data = &output;
	rbuf_set_ptr(&tokens, tokens.size++, output_token);
	struct Token* input_token = new_token(TOKEN_FUNC, &curr_token_id);
	input_token->data = &input;
	rbuf_set_ptr(&tokens, tokens.size++, input_token);

	//TODO find a better way to do val/func evaluation that does not involve leaky scopes
	Value interpreter_curr_val = 0;

	bool has_tokens = tokens.size;
	while (has_tokens){
		struct Token* token = rbuf_get_ptr(&tokens, 0);
		//DEBUG/print_token_buf(&tokens);
		//DEBUG/printf(" << %lu\n", tokens.size);
		if (curr_token_id > max_token_id){
			break;
		}

		switch (token->type){
		case TOKEN_S:
			{
			if (tokens.size >= 4){
				//[0] = (y z)
				struct Token* temp = new_token(TOKEN_WRAP, &curr_token_id);
				((struct Token**)temp->data)[0] = rbuf_get_ptr(&tokens, 2);
				++((struct Token*)rbuf_get_ptr(&tokens, 2))->ref_count;
				((struct Token**)temp->data)[1] = rbuf_get_ptr(&tokens, 3);
				++((struct Token*)rbuf_get_ptr(&tokens, 3))->ref_count;

				free_token(rbuf_get_ptr(&tokens, 0));
				rbuf_set_ptr(&tokens, 0, temp);

				//[2] = z
				free_token(rbuf_get_ptr(&tokens, 2));
				rbuf_set_ptr(&tokens, 2, rbuf_get_ptr(&tokens, 3));
				++((struct Token*)rbuf_get_ptr(&tokens, 2))->ref_count;

				//[3] = [0]
				free_token(rbuf_get_ptr(&tokens, 3));
				rbuf_set_ptr(&tokens, 3, rbuf_get_ptr(&tokens, 0));
				++((struct Token*)rbuf_get_ptr(&tokens, 3))->ref_count;

				//advance tokens
				free_token(rbuf_get_ptr(&tokens, 0));
				rbuf_advance(&tokens, 1);
			}
			else{
				has_tokens = false;
			}
			}
			break;
		case TOKEN_K:
			{
			if (tokens.size >= 3){
				//[2] = x
				free_token(rbuf_get_ptr(&tokens, 2));
				rbuf_set_ptr(&tokens, 2, rbuf_get_ptr(&tokens, 1));
				++((struct Token*)rbuf_get_ptr(&tokens, 2))->ref_count;

				//advance tokens
				free_token(rbuf_get_ptr(&tokens, 0));
				free_token(rbuf_get_ptr(&tokens, 1));
				rbuf_advance(&tokens, 2);
			}
			else{
				has_tokens = false;
			}
			}
			break;
		case TOKEN_I:
			{
			if (tokens.size >= 2){
				//advance tokens
				free_token(rbuf_get_ptr(&tokens, 0));
				rbuf_advance(&tokens, 1);
			}
			else{
				has_tokens = false;
			}
			}
			break;
		case TOKEN_WRAP:
			{
			if (tokens.size + token->len > tokens.capacity){
				rbuf_resize_ptr(&tokens, tokens.size + token->len - 1);
				token = rbuf_get_ptr(&tokens, 0);
			}
			//MAGIC +1 so that we overwrite tokens[0]
			int start_token = tokens.capacity - token->len + 1;

			//DEBUG/printf("Unwrapping: ");
			for (int i = 0; i < token->len; ++i){
				//DEBUG/printf("%u, ", ((struct Token**)token->data)[i]->type);
				rbuf_set_ptr(&tokens, start_token + i, ((void**)token->data)[i]);
				++((struct Token**)token->data)[i]->ref_count;
			}
			//DEBUG/printf("\n");

			tokens.offset += start_token;
			tokens.offset %= tokens.capacity;
			tokens.size += token->len;

			free_token(token);
			--tokens.size;
			}
			break;
		case TOKEN_LAZY_WRAP:
			{
			has_tokens &= lazy_unwrap(&tokens, &curr_token_id, 0);
			}
			break;
		case TOKEN_FUNC:
			{
			Value (*func)(Value) = token->data;
			interpreter_curr_val = func(interpreter_curr_val);
			
			//advance tokens
			free_token(rbuf_get_ptr(&tokens, 0));
			rbuf_advance(&tokens, 1);
			}
			break;
		case TOKEN_VAL:
			{
			interpreter_curr_val = token->val;

			//advance tokens
			free_token(rbuf_get_ptr(&tokens, 0));
			rbuf_advance(&tokens, 1);
			}
			break;
		case TOKEN_NULL:
		default:
			printf("?\n");
			has_tokens = false;
		}

		if (!has_tokens){
			printf("Incomplete parse\n");
		}
		has_tokens &= !!tokens.size;
	}

	//print buffer and info at the end
	print_token_buf(&tokens);
	printf("\n%lu %lu\n", tokens.size, curr_token_id);

	//free all memory in rbuf
	while (tokens.size){
		free_token(rbuf_get_ptr(&tokens, 0));
		rbuf_advance(&tokens, 1);
	}
	free(tokens.data);

	return 0;
}
