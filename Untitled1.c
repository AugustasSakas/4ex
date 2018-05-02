#define _BSD_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>

#define DB_FILE "animals.db"

struct pool {
    uint32_t size;
    uint32_t fill;
    char *buffer;
};

#define POOL_INIT {0, 0, NULL}

uint32_t
pool_push(struct pool *p, const char *s)
{
    size_t size = strlen(s) + 1;
    while (p->fill + size > p->size) {
        if (p->size == 0)
            p->size = 4096;
        else
            p->size *= 2;
        p->buffer = realloc(p->buffer, p->size);
    }
    strcpy(p->buffer + p->fill, s);
    p->fill += size;
    return p->fill - size;
}

void
pool_save(const struct pool *p, FILE *out)
{
    uint32_t fill = htole32(p->fill);
    fwrite(&fill, sizeof(fill), 1, out);
    fwrite(p->buffer, p->fill, 1, out);
}

void
pool_load(struct pool *p, FILE *in)
{
    fread(&p->fill, sizeof(p->fill), 1, in);
    p->size = p->fill = le32toh(p->fill);
    p->buffer = malloc(p->size);
    fread(p->buffer, p->fill, 1, in);
}

void
pool_free(struct pool *p)
{
    free(p->buffer);
    p->buffer = NULL;
}


enum type { QUESTION, ANSWER };

struct node {
    enum type type;
    uint32_t text;
    uint32_t yes;
    uint32_t no;
};

struct db {
    uint32_t max;
    uint32_t count;
    struct node *nodes;
    struct pool pool;
};

#define DB_INIT {0, 0, NULL, POOL_INIT}

void
db_free(struct db *db)
{
    free(db->nodes);
    db->nodes = NULL;
    pool_free(&db->pool);
}

uint32_t
db_push(struct db *db, const struct node *node)
{
    if (db->count == db->max) {
        if (db->max == 0)
            db->max = 16;
        else
            db->max *= 2;
        size_t size = sizeof(db->nodes[0]) * db->max;
        db->nodes = realloc(db->nodes, size);
    }
    db->nodes[db->count] = *node;
    return db->count++;
}

void
db_save(const struct db *db, FILE *out)
{
    uint32_t count = htole32(db->count);
    fwrite(&count, sizeof(count), 1, out);
    for (uint32_t i = 0; i < db->count; i++) {
        uint8_t type = db->nodes[i].type;
        fwrite(&type, sizeof(type), 1, out);
        uint32_t text = htole32(db->nodes[i].text);
        fwrite(&text, sizeof(text), 1, out);
        if (db->nodes[i].type == QUESTION) {
            uint32_t yn[2] = {
                htole32(db->nodes[i].yes),
                htole32(db->nodes[i].no)
            };
            fwrite(yn, sizeof(yn), 1, out);
        }
    }
    pool_save(&db->pool, out);
}

void
db_load(struct db *db, FILE *in)
{
    fread(&db->count, sizeof(db->count), 1, in);
    db->max = db->count = le32toh(db->count);
    db->nodes = malloc(sizeof(db->nodes[0]) * db->max);
    for (uint32_t i = 0; i < db->count; i++) {
        uint8_t type;
        fread(&type, sizeof(type), 1, in);
        db->nodes[i].type = type;
        uint32_t text;
        fread(&text, sizeof(text), 1, in);
        db->nodes[i].text = le32toh(text);
        if (db->nodes[i].type == QUESTION) {
            uint32_t yn[2];
            fread(yn, sizeof(yn), 1, in);
            db->nodes[i].yes = le32toh(yn[0]);
            db->nodes[i].no = le32toh(yn[1]);
        }
    }
    pool_load(&db->pool, in);
}

void
db_split(struct db *db, uint32_t i, const char *question, const char *animal)
{
    struct node new_animal = {
        .type = ANSWER,
        .text = pool_push(&db->pool, animal)
    };
    struct node new_question = {
        .type = QUESTION,
        .text = pool_push(&db->pool, question),
        .yes = db_push(db, &new_animal),
        .no = db_push(db, &db->nodes[i])
    };
    db->nodes[i] = new_question;
}

const char *
db_string(const struct db *db, uint32_t address)
{
    return db->pool.buffer + address;
}


enum response { NONE, QUIT, NO, YES };

enum response
input(void)
{
    fflush(stdout);
    enum response result = NONE;
    bool done = false;
    while (!done) {
        int c = getchar();
        switch (c) {
            case 'y':
            case 'Y':
                result = YES;
                break;
            case 'n':
            case 'N':
                result = NO;
                break;
            case 'q':
            case 'Q':
                result = QUIT;
                done = true;
                break;
            case '\n':
                done = result != NONE;
                break;
        }
    }
    return result;
}

void
chomp(char *s)
{
    char *p = s;
    while (*p)
        p++;
    for (p--; p > s && *p == '\n'; p--)
        *p = '\0';
}

int main(void)
{
    struct db db = DB_INIT;
    FILE *dbfile = fopen(DB_FILE, "rb");
    if (dbfile != NULL) {
        db_load(&db, dbfile);
        fclose(dbfile);
    } else {
        struct node node = {
            .type = ANSWER,
            .text = pool_push(&db.pool, "elephant")
        };
        db_push(&db, &node);
    }

    printf("Welcome to Animal Guess. Please think of an Animal.\n");
    printf("Hit 'y' to proceed -> ");
    while (input() != YES);

    uint32_t n = 0;
    bool running = true;
    while (running) {
        switch (db.nodes[n].type) {
            case ANSWER:
                printf("I think your animal is %s. Am I correct? -> ",
                       db_string(&db, db.nodes[n].text));
                if (input() == YES) {
                    printf("I win!\n");
                } else {
                    printf("Darnit!\nWhat animal were you thinking of? -> ");
                    fflush(stdout);
                    char animal[256];
                    fgets(animal, sizeof(animal), stdin);
                    chomp(animal);
                    printf("A unique question that answers yes for %s -> \n",
                           animal);
                    char question[512];
                    fgets(question, sizeof(question), stdin);
                    chomp(question);
                    db_split(&db, n, question, animal);
                }
                printf("Play again? -> ");
                if (input() == YES)
                    n = 0;
                else
                    running = false;
                break;
            case QUESTION:
                printf("%s -> ", db_string(&db, db.nodes[n].text));
                enum response response = input();
                if (response == YES)
                    n = db.nodes[n].yes;
                else if (response == NO)
                    n = db.nodes[n].no;
                else
                    running = false;
                break;
        }
    }

    dbfile = fopen(DB_FILE, "wb");
    db_save(&db, dbfile);
    db_free(&db);
    fclose(dbfile);
    return 0;
}
