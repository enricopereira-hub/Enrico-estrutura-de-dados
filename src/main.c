#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

// ==========================================
// 1. COMPATIBILIDADE DE SISTEMA
// ==========================================
#ifdef _WIN32
    #include <windows.h>
    void dormir_ms(int ms) { Sleep(ms); }
    double obter_tempo_ms() {
        LARGE_INTEGER freq, count;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&count);
        return (double)count.QuadPart / freq.QuadPart * 1000.0;
    }
#else
    #include <unistd.h>
    #include <sys/time.h>
    void dormir_ms(int ms) { usleep(ms * 1000); }
    double obter_tempo_ms() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
    }
#endif

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

// ==========================================
// 2. ESTRUTURAS DE DADOS BÁSICAS
// ==========================================
typedef struct {
    long long timestamp;
    int sensor_id;
    double valor;
} LeituraSensor;

typedef struct {
    LeituraSensor* dados;
    int tamanho;
    int capacidade;
} VetorLeituras;

// [FIX J] Toda alocação crítica verifica NULL e aborta com mensagem clara
void vetor_init(VetorLeituras* v, int capacidade_inicial) {
    v->capacidade = capacidade_inicial;
    v->tamanho = 0;
    v->dados = (LeituraSensor*)malloc(v->capacidade * sizeof(LeituraSensor));
    if (!v->dados) { fprintf(stderr, "FATAL: malloc falhou em vetor_init\n"); exit(1); }
}

void vetor_push(VetorLeituras* v, LeituraSensor leitura) {
    if (v->tamanho >= v->capacidade) {
        v->capacidade *= 2;
        LeituraSensor* tmp = (LeituraSensor*)realloc(v->dados, v->capacidade * sizeof(LeituraSensor));
        if (!tmp) { fprintf(stderr, "FATAL: realloc falhou em vetor_push\n"); exit(1); } // [FIX J]
        v->dados = tmp;
    }
    v->dados[v->tamanho++] = leitura;
}

void vetor_free(VetorLeituras* v) {
    free(v->dados);
    v->dados = NULL;
    v->tamanho = 0;
    v->capacidade = 0;
}

double aplicarRuido(double valorBase, double media, double desvioPadrao) {
    static double z1;
    static bool generate = false;
    generate = !generate;
    if (!generate) return valorBase + z1 * desvioPadrao + media;
    double u1, u2;
    do { u1 = rand() * (1.0 / RAND_MAX); } while (u1 <= 1e-7);
    u2 = rand() * (1.0 / RAND_MAX);
    double z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    z1     = sqrt(-2.0 * log(u1)) * sin(2.0 * M_PI * u2);
    return valorBase + z0 * desvioPadrao + media;
}

// ==========================================
// 3. ESTRUTURAS AVANÇADAS
// ==========================================

// --- LISTA ENCADEADA ---
typedef struct NoListaSimples {
    LeituraSensor dado;
    struct NoListaSimples* prox;
} NoListaSimples;

typedef struct {
    NoListaSimples* inicio;
    int tamanho;
} ListaSensores;

void Lista_init(ListaSensores* lista) { lista->inicio = NULL; lista->tamanho = 0; }

void Lista_inserir(ListaSensores* lista, LeituraSensor leitura) {
    NoListaSimples* novo = (NoListaSimples*)malloc(sizeof(NoListaSimples));
    if (!novo) { fprintf(stderr, "FATAL: malloc falhou em Lista_inserir\n"); exit(1); } // [FIX J]
    novo->dado = leitura;
    novo->prox = lista->inicio;
    lista->inicio = novo;
    lista->tamanho++;
}

bool Lista_buscar(ListaSensores* lista, long long ts, LeituraSensor* resultado) {
    NoListaSimples* atual = lista->inicio;
    while (atual != NULL) {
        if (atual->dado.timestamp == ts) { *resultado = atual->dado; return true; }
        atual = atual->prox;
    }
    return false;
}

bool Lista_remover(ListaSensores* lista, long long ts) {
    NoListaSimples* atual = lista->inicio;
    NoListaSimples* anterior = NULL;
    while (atual != NULL) {
        if (atual->dado.timestamp == ts) {
            if (anterior == NULL) lista->inicio = atual->prox;
            else anterior->prox = atual->prox;
            free(atual);
            lista->tamanho--;
            return true;
        }
        anterior = atual;
        atual = atual->prox;
    }
    return false;
}

void Lista_free(ListaSensores* lista) {
    NoListaSimples* atual = lista->inicio;
    while (atual != NULL) {
        NoListaSimples* prox = atual->prox;
        free(atual);
        atual = prox;
    }
    lista->inicio = NULL;
    lista->tamanho = 0;
}

// --- TABELA HASH COM ENCADEAMENTO ---
typedef struct NoLista {
    LeituraSensor dado;
    struct NoLista* prox;
} NoLista;

typedef struct {
    int capacidade;
    int tamanho;
    int totalColisoes;
    NoLista** tabela;
} TabelaHashSensores;

void Hash_init(TabelaHashSensores* th, int capacidade) {
    th->capacidade = capacidade;
    th->tamanho = 0;
    th->totalColisoes = 0;
    th->tabela = (NoLista**)calloc(th->capacidade, sizeof(NoLista*));
    if (!th->tabela) { fprintf(stderr, "FATAL: calloc falhou em Hash_init\n"); exit(1); } // [FIX J]
}

int Hash_calcular(TabelaHashSensores* th, long long chave) {
    return (int)(((unsigned long long)chave) % (unsigned long long)th->capacidade);
}

void Hash_inserir(TabelaHashSensores* th, LeituraSensor leitura) {
    int indice = Hash_calcular(th, leitura.timestamp);
    if (th->tabela[indice] != NULL) th->totalColisoes++;
    NoLista* novoNo = (NoLista*)malloc(sizeof(NoLista));
    if (!novoNo) { fprintf(stderr, "FATAL: malloc falhou em Hash_inserir\n"); exit(1); } // [FIX J]
    novoNo->dado = leitura;
    novoNo->prox = th->tabela[indice];
    th->tabela[indice] = novoNo;
    th->tamanho++;
}

bool Hash_buscar(TabelaHashSensores* th, long long timestampDesejado, LeituraSensor* resultado) {
    int indice = Hash_calcular(th, timestampDesejado);
    NoLista* atual = th->tabela[indice];
    while (atual != NULL) {
        if (atual->dado.timestamp == timestampDesejado) { *resultado = atual->dado; return true; }
        atual = atual->prox;
    }
    return false;
}

bool Hash_remover(TabelaHashSensores* th, long long timestampDesejado) {
    int indice = Hash_calcular(th, timestampDesejado);
    NoLista* atual = th->tabela[indice];
    NoLista* anterior = NULL;
    while (atual != NULL) {
        if (atual->dado.timestamp == timestampDesejado) {
            if (anterior == NULL) th->tabela[indice] = atual->prox;
            else anterior->prox = atual->prox;
            free(atual);
            th->tamanho--;
            return true;
        }
        anterior = atual;
        atual = atual->prox;
    }
    return false;
}

void Hash_free(TabelaHashSensores* th) {
    for (int i = 0; i < th->capacidade; i++) {
        NoLista* atual = th->tabela[i];
        while (atual != NULL) {
            NoLista* prox = atual->prox;
            free(atual);
            atual = prox;
        }
    }
    free(th->tabela);
}

// --- CUCKOO HASH ---
#define CAPACIDADE_CUCKOO 30011

typedef struct {
    LeituraSensor* tabela1;
    LeituraSensor* tabela2;
    bool* ocupado1;
    bool* ocupado2;
} CuckooHash;

int hash1(long long chave) {
    return (int)(((unsigned long long)chave) % (unsigned long long)CAPACIDADE_CUCKOO);
}

// [FIX C] hash2 agora usa hash multiplicativo verdadeiramente independente de hash1.
// A constante 2654435761 = floor(2^32 / phi) é derivada da razão áurea e garante
// excelente dispersão de bits (avalanche effect), eliminando a correlação estrutural
// que existia na versão anterior (chave / CAPACIDADE_CUCKOO) para timestamps sequenciais.
int hash2(long long chave) {
    unsigned long long k = (unsigned long long)chave;
    return (int)((k * 2654435761ULL) % (unsigned long long)CAPACIDADE_CUCKOO);
}

// [FIX B] Retorno de bool verificado nos sites de chamada para contabilizar falhas
bool Cuckoo_inserir(CuckooHash* ch, LeituraSensor leitura) {
    LeituraSensor atual = leitura;
    for (int n = 0; n < 50; n++) {
        int i = hash1(atual.timestamp);
        if (!ch->ocupado1[i]) { ch->tabela1[i] = atual; ch->ocupado1[i] = true; return true; }
        LeituraSensor temp = ch->tabela1[i];
        ch->tabela1[i] = atual;
        atual = temp;

        i = hash2(atual.timestamp);
        if (!ch->ocupado2[i]) { ch->tabela2[i] = atual; ch->ocupado2[i] = true; return true; }
        temp = ch->tabela2[i];
        ch->tabela2[i] = atual;
        atual = temp;
    }
    return false; // Falha: rehash necessário
}

bool Cuckoo_buscar(CuckooHash* ch, long long ts, LeituraSensor* resultado) {
    int i1 = hash1(ts);
    if (ch->ocupado1[i1] && ch->tabela1[i1].timestamp == ts) { *resultado = ch->tabela1[i1]; return true; }
    int i2 = hash2(ts);
    if (ch->ocupado2[i2] && ch->tabela2[i2].timestamp == ts) { *resultado = ch->tabela2[i2]; return true; }
    return false;
}

bool Cuckoo_remover(CuckooHash* ch, long long ts) {
    int i1 = hash1(ts);
    if (ch->ocupado1[i1] && ch->tabela1[i1].timestamp == ts) { ch->ocupado1[i1] = false; return true; }
    int i2 = hash2(ts);
    if (ch->ocupado2[i2] && ch->tabela2[i2].timestamp == ts) { ch->ocupado2[i2] = false; return true; }
    return false;
}

void Cuckoo_free(CuckooHash* ch) {
    free(ch->tabela1); free(ch->tabela2);
    free(ch->ocupado1); free(ch->ocupado2);
}

// --- ÁRVORE AVL ---
typedef struct NoAVL {
    long long chave_timestamp;
    LeituraSensor dado;
    struct NoAVL* esquerda;
    struct NoAVL* direita;
    int altura;
} NoAVL;

typedef struct {
    NoAVL* raiz;
    int tamanho;
} ArvoreAVLSensores;

void AVL_init(ArvoreAVLSensores* arvore) { arvore->raiz = NULL; arvore->tamanho = 0; }

int AVL_altura(NoAVL* no) { return no ? no->altura : 0; }
int max_int(int a, int b) { return a > b ? a : b; }
int AVL_fatorBalanceamento(NoAVL* no) { return no ? AVL_altura(no->esquerda) - AVL_altura(no->direita) : 0; }

NoAVL* AVL_rotacaoDireita(NoAVL* y) {
    NoAVL* x = y->esquerda; NoAVL* T2 = x->direita;
    x->direita = y; y->esquerda = T2;
    y->altura = max_int(AVL_altura(y->esquerda), AVL_altura(y->direita)) + 1;
    x->altura = max_int(AVL_altura(x->esquerda), AVL_altura(x->direita)) + 1;
    return x;
}

NoAVL* AVL_rotacaoEsquerda(NoAVL* x) {
    NoAVL* y = x->direita; NoAVL* T2 = y->esquerda;
    y->esquerda = x; x->direita = T2;
    x->altura = max_int(AVL_altura(x->esquerda), AVL_altura(x->direita)) + 1;
    y->altura = max_int(AVL_altura(y->esquerda), AVL_altura(y->direita)) + 1;
    return y;
}

NoAVL* AVL_inserirInterno(NoAVL* no, long long ts, LeituraSensor leitura, int* cresceu) {
    if (!no) {
        NoAVL* novo = (NoAVL*)malloc(sizeof(NoAVL));
        if (!novo) { fprintf(stderr, "FATAL: malloc falhou em AVL_inserirInterno\n"); exit(1); } // [FIX J]
        novo->chave_timestamp = ts; novo->dado = leitura;
        novo->esquerda = novo->direita = NULL; novo->altura = 1;
        *cresceu = 1;
        return novo;
    }
    if (ts < no->chave_timestamp) no->esquerda = AVL_inserirInterno(no->esquerda, ts, leitura, cresceu);
    else if (ts > no->chave_timestamp) no->direita = AVL_inserirInterno(no->direita, ts, leitura, cresceu);
    else return no;
    no->altura = 1 + max_int(AVL_altura(no->esquerda), AVL_altura(no->direita));
    int b = AVL_fatorBalanceamento(no);
    if (b > 1  && ts < no->esquerda->chave_timestamp) return AVL_rotacaoDireita(no);
    if (b < -1 && ts > no->direita->chave_timestamp)  return AVL_rotacaoEsquerda(no);
    if (b > 1  && ts > no->esquerda->chave_timestamp) { no->esquerda = AVL_rotacaoEsquerda(no->esquerda); return AVL_rotacaoDireita(no); }
    if (b < -1 && ts < no->direita->chave_timestamp)  { no->direita  = AVL_rotacaoDireita(no->direita);  return AVL_rotacaoEsquerda(no); }
    return no;
}

void AVL_inserir(ArvoreAVLSensores* arvore, LeituraSensor leitura) {
    int cresceu = 0;
    arvore->raiz = AVL_inserirInterno(arvore->raiz, leitura.timestamp, leitura, &cresceu);
    if (cresceu) arvore->tamanho++;
}

NoAVL* AVL_minimo(NoAVL* no) { while (no->esquerda) no = no->esquerda; return no; }

NoAVL* AVL_removerInterno(NoAVL* raiz, long long ts, int* diminuiu) {
    if (!raiz) return raiz;
    if (ts < raiz->chave_timestamp) raiz->esquerda = AVL_removerInterno(raiz->esquerda, ts, diminuiu);
    else if (ts > raiz->chave_timestamp) raiz->direita = AVL_removerInterno(raiz->direita, ts, diminuiu);
    else {
        *diminuiu = 1;
        if (!raiz->esquerda || !raiz->direita) {
            NoAVL* temp = raiz->esquerda ? raiz->esquerda : raiz->direita;
            if (!temp) { temp = raiz; raiz = NULL; } else *raiz = *temp;
            free(temp);
        } else {
            NoAVL* temp = AVL_minimo(raiz->direita);
            raiz->chave_timestamp = temp->chave_timestamp;
            raiz->dado = temp->dado;
            raiz->direita = AVL_removerInterno(raiz->direita, temp->chave_timestamp, diminuiu);
        }
    }
    if (!raiz) return raiz;
    raiz->altura = 1 + max_int(AVL_altura(raiz->esquerda), AVL_altura(raiz->direita));
    int b = AVL_fatorBalanceamento(raiz);
    if (b > 1  && AVL_fatorBalanceamento(raiz->esquerda) >= 0) return AVL_rotacaoDireita(raiz);
    if (b > 1  && AVL_fatorBalanceamento(raiz->esquerda) <  0) { raiz->esquerda = AVL_rotacaoEsquerda(raiz->esquerda); return AVL_rotacaoDireita(raiz); }
    if (b < -1 && AVL_fatorBalanceamento(raiz->direita)  <= 0) return AVL_rotacaoEsquerda(raiz);
    if (b < -1 && AVL_fatorBalanceamento(raiz->direita)  >  0) { raiz->direita  = AVL_rotacaoDireita(raiz->direita);  return AVL_rotacaoEsquerda(raiz); }
    return raiz;
}

void AVL_remover(ArvoreAVLSensores* arvore, long long ts) {
    int diminuiu = 0;
    arvore->raiz = AVL_removerInterno(arvore->raiz, ts, &diminuiu);
    if (diminuiu) arvore->tamanho--;
}

bool AVL_buscarInterno(NoAVL* no, long long ts, LeituraSensor* resultado) {
    if (!no) return false;
    if (ts == no->chave_timestamp) { *resultado = no->dado; return true; }
    return ts < no->chave_timestamp
        ? AVL_buscarInterno(no->esquerda, ts, resultado)
        : AVL_buscarInterno(no->direita,  ts, resultado);
}

bool AVL_buscar(ArvoreAVLSensores* arvore, long long ts, LeituraSensor* resultado) {
    return AVL_buscarInterno(arvore->raiz, ts, resultado);
}

void AVL_freeInterno(NoAVL* no) {
    if (no) { AVL_freeInterno(no->esquerda); AVL_freeInterno(no->direita); free(no); }
}

void AVL_free(ArvoreAVLSensores* arvore) {
    AVL_freeInterno(arvore->raiz);
    arvore->raiz = NULL;
    arvore->tamanho = 0;
}

// --- MAX HEAP ---
typedef struct {
    LeituraSensor* heap;
    int tamanho;
    int capacidade;
} HeapSensores;

void Heap_init(HeapSensores* h, int capacidade_inicial) {
    h->tamanho = 0; h->capacidade = capacidade_inicial;
    h->heap = (LeituraSensor*)malloc(capacidade_inicial * sizeof(LeituraSensor));
    if (!h->heap) { fprintf(stderr, "FATAL: malloc falhou em Heap_init\n"); exit(1); } // [FIX J]
}
void Heap_free(HeapSensores* h) { free(h->heap); }
int Heap_pai(int i) { return (i - 1) / 2; }
int Heap_filhoEsq(int i) { return 2 * i + 1; }
int Heap_filhoDir(int i) { return 2 * i + 2; }
void Heap_swap(LeituraSensor* a, LeituraSensor* b) { LeituraSensor t = *a; *a = *b; *b = t; }

void Heap_subir(HeapSensores* h, int i) {
    while (i > 0 && h->heap[Heap_pai(i)].valor < h->heap[i].valor) {
        Heap_swap(&h->heap[i], &h->heap[Heap_pai(i)]);
        i = Heap_pai(i);
    }
}

void Heap_descer(HeapSensores* h, int i) {
    int m = i;
    int e = Heap_filhoEsq(i), d = Heap_filhoDir(i);
    if (e < h->tamanho && h->heap[e].valor > h->heap[m].valor) m = e;
    if (d < h->tamanho && h->heap[d].valor > h->heap[m].valor) m = d;
    if (m != i) { Heap_swap(&h->heap[i], &h->heap[m]); Heap_descer(h, m); }
}

void Heap_inserir(HeapSensores* h, LeituraSensor leitura) {
    if (h->tamanho >= h->capacidade) {
        h->capacidade *= 2;
        LeituraSensor* tmp = (LeituraSensor*)realloc(h->heap, h->capacidade * sizeof(LeituraSensor));
        if (!tmp) { fprintf(stderr, "FATAL: realloc falhou em Heap_inserir\n"); exit(1); } // [FIX J]
        h->heap = tmp;
    }
    h->heap[h->tamanho] = leitura;
    Heap_subir(h, h->tamanho);
    h->tamanho++;
}

int Heap_tamanho(HeapSensores* h) { return h->tamanho; }

bool Heap_extrairMaximo(HeapSensores* h, LeituraSensor* resultado) {
    if (h->tamanho == 0) return false;
    *resultado = h->heap[0];
    h->heap[0] = h->heap[--h->tamanho];
    if (h->tamanho > 0) Heap_descer(h, 0);
    return true;
}

// --- SKIP LIST ---
#define MAX_LEVEL 16
#define SKIP_P 0.5

typedef struct NoSkip {
    long long chave_timestamp;
    LeituraSensor dado;
    struct NoSkip** proximo;
} NoSkip;

typedef struct {
    NoSkip* cabeca;
    int nivelAtual;
} SkipListSensores;

NoSkip* criarNoSkip(long long ts, LeituraSensor leitura, int nivel) {
    NoSkip* no = (NoSkip*)malloc(sizeof(NoSkip));
    if (!no) { fprintf(stderr, "FATAL: malloc falhou em criarNoSkip\n"); exit(1); } // [FIX J]
    no->chave_timestamp = ts; no->dado = leitura;
    no->proximo = (NoSkip**)calloc(nivel + 1, sizeof(NoSkip*));
    if (!no->proximo) { fprintf(stderr, "FATAL: calloc falhou em criarNoSkip\n"); exit(1); } // [FIX J]
    return no;
}

void SkipList_init(SkipListSensores* sl) {
    sl->nivelAtual = 0;
    LeituraSensor vazia = {0, 0, 0.0};
    sl->cabeca = criarNoSkip(-1, vazia, MAX_LEVEL);
}

int sortearNivel() {
    int nivel = 0;
    float r = (float)rand() / RAND_MAX;
    while (r < SKIP_P && nivel < MAX_LEVEL) { nivel++; r = (float)rand() / RAND_MAX; }
    return nivel;
}

void SkipList_inserir(SkipListSensores* sl, LeituraSensor leitura) {
    NoSkip* atualizacao[MAX_LEVEL + 1];
    NoSkip* atual = sl->cabeca;
    for (int i = sl->nivelAtual; i >= 0; i--) {
        while (atual->proximo[i] && atual->proximo[i]->chave_timestamp < leitura.timestamp)
            atual = atual->proximo[i];
        atualizacao[i] = atual;
    }
    atual = atual->proximo[0];
    if (!atual || atual->chave_timestamp != leitura.timestamp) {
        int novoNivel = sortearNivel();
        if (novoNivel > sl->nivelAtual) {
            for (int i = sl->nivelAtual + 1; i <= novoNivel; i++) atualizacao[i] = sl->cabeca;
            sl->nivelAtual = novoNivel;
        }
        NoSkip* novoNo = criarNoSkip(leitura.timestamp, leitura, novoNivel);
        for (int i = 0; i <= novoNivel; i++) {
            novoNo->proximo[i] = atualizacao[i]->proximo[i];
            atualizacao[i]->proximo[i] = novoNo;
        }
    }
}

bool SkipList_remover(SkipListSensores* sl, long long ts) {
    NoSkip* atualizacao[MAX_LEVEL + 1];
    NoSkip* atual = sl->cabeca;
    for (int i = sl->nivelAtual; i >= 0; i--) {
        while (atual->proximo[i] && atual->proximo[i]->chave_timestamp < ts)
            atual = atual->proximo[i];
        atualizacao[i] = atual;
    }
    atual = atual->proximo[0];
    if (atual && atual->chave_timestamp == ts) {
        for (int i = 0; i <= sl->nivelAtual; i++) {
            if (atualizacao[i]->proximo[i] != atual) break;
            atualizacao[i]->proximo[i] = atual->proximo[i];
        }
        free(atual->proximo); free(atual);
        while (sl->nivelAtual > 0 && !sl->cabeca->proximo[sl->nivelAtual]) sl->nivelAtual--;
        return true;
    }
    return false;
}

bool SkipList_buscar(SkipListSensores* sl, long long ts, LeituraSensor* resultado) {
    NoSkip* atual = sl->cabeca;
    for (int i = sl->nivelAtual; i >= 0; i--)
        while (atual->proximo[i] && atual->proximo[i]->chave_timestamp < ts)
            atual = atual->proximo[i];
    atual = atual->proximo[0];
    if (atual && atual->chave_timestamp == ts) { *resultado = atual->dado; return true; }
    return false;
}

void SkipList_free(SkipListSensores* sl) {
    NoSkip* atual = sl->cabeca;
    while (atual) {
        NoSkip* prox = atual->proximo[0];
        free(atual->proximo); free(atual);
        atual = prox;
    }
    sl->cabeca = NULL; sl->nivelAtual = 0;
}

// --- SEGMENT TREE ---
typedef struct {
    double* arvore;
    int n;
} SegmentTreeSensores;

double max_double(double a, double b) { return a > b ? a : b; }

void SegmentTree_construirInterno(SegmentTreeSensores* st, const VetorLeituras* dados,
                                  int no, int inicio, int fim) {
    if (inicio == fim) {
        st->arvore[no] = dados->dados[inicio].valor;
    } else {
        int meio = (inicio + fim) / 2;
        SegmentTree_construirInterno(st, dados, 2 * no + 1, inicio, meio);
        SegmentTree_construirInterno(st, dados, 2 * no + 2, meio + 1, fim);
        st->arvore[no] = max_double(st->arvore[2 * no + 1], st->arvore[2 * no + 2]);
    }
}

void SegmentTree_init(SegmentTreeSensores* st, const VetorLeituras* dados) {
    st->n = dados->tamanho;
    st->arvore = (double*)calloc(4 * st->n, sizeof(double));
    if (!st->arvore) { fprintf(stderr, "FATAL: calloc falhou em SegmentTree_init\n"); exit(1); } // [FIX J]
    SegmentTree_construirInterno(st, dados, 0, 0, st->n - 1);
}

void SegmentTree_free(SegmentTreeSensores* st) { free(st->arvore); }

double SegmentTree_consultarInterno(SegmentTreeSensores* st, int no, int inicio, int fim, int l, int r) {
    if (l > fim || r < inicio) return -1.0;
    if (l <= inicio && r >= fim) return st->arvore[no];
    int meio = (inicio + fim) / 2;
    return max_double(
        SegmentTree_consultarInterno(st, 2 * no + 1, inicio, meio,    l, r),
        SegmentTree_consultarInterno(st, 2 * no + 2, meio + 1, fim,   l, r)
    );
}

void SegmentTree_removerInterno(SegmentTreeSensores* st, int no, int inicio, int fim, int indice) {
    if (inicio == fim) {
        st->arvore[no] = -1.0; // Marca posição como inválida (remoção lógica)
    } else {
        int meio = (inicio + fim) / 2;
        if (indice <= meio) SegmentTree_removerInterno(st, 2 * no + 1, inicio, meio,    indice);
        else                SegmentTree_removerInterno(st, 2 * no + 2, meio + 1, fim,   indice);
        st->arvore[no] = max_double(st->arvore[2 * no + 1], st->arvore[2 * no + 2]);
    }
}

void SegmentTree_remover(SegmentTreeSensores* st, int indice) {
    if (indice >= 0 && indice < st->n)
        SegmentTree_removerInterno(st, 0, 0, st->n - 1, indice);
}

// ==========================================
// [FIX G] LIMIAR ESTATÍSTICO DE ANOMALIA
// ==========================================

// Comparador auxiliar para qsort de doubles
int compararDoubles(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/*
 * [FIX G] calcularLimiarAnomalia
 *
 * Substitui o valor mágico 1000.0 por um critério estatístico robusto:
 * a cerca "far out" de Tukey (Q3 + 3 * IQR).
 *
 * Por que IQR e não z-score simples?
 *   - Com 10% de anomalias em 9999.99, a média e o desvio padrão ficam
 *     fortemente enviesados pelos próprios outliers, tornando o z-score
 *     ineficaz para encontrá-los.
 *   - Mediana (Q2) e IQR são resistentes a até ~25% de outliers, pois
 *     dependem da ordem e não da magnitude dos valores.
 *
 * Resultado esperado para o dataset atual (~30 ± ruído com 10% em 9999.99):
 *   Q1 ≈ 31, Q3 ≈ 37, IQR ≈ 6 → limiar ≈ 37 + 18 = 55
 *   Isso filtra corretamente 9999.99 enquanto preserva toda a faixa válida.
 */
double calcularLimiarAnomalia(VetorLeituras* dataset) {
    if (dataset->tamanho == 0) return 1000.0;

    int TAMANHO_AMOSTRA = (dataset->tamanho < 500) ? dataset->tamanho : 500;
    double* amostra = (double*)malloc(TAMANHO_AMOSTRA * sizeof(double));
    if (!amostra) return 1000.0; // fallback seguro se malloc falhar

    // Amostragem sistemática para cobrir toda a distribuição temporal
    for (int i = 0; i < TAMANHO_AMOSTRA; i++) {
        int idx = (int)((long long)i * dataset->tamanho / TAMANHO_AMOSTRA);
        amostra[i] = dataset->dados[idx].valor;
    }

    qsort(amostra, TAMANHO_AMOSTRA, sizeof(double), compararDoubles);

    double q1  = amostra[TAMANHO_AMOSTRA / 4];
    double q3  = amostra[3 * TAMANHO_AMOSTRA / 4];
    double iqr = q3 - q1;

    free(amostra);
    return q3 + 3.0 * iqr; // Cerca "far out": captura apenas outliers extremos
}

// ==========================================
// MÓDULO DE ANÁLISE — [FIX G] limiarAnomalia no lugar de 1000.0 hardcoded
// ==========================================
void calcularEstatisticasGerais(VetorLeituras* dataset, double limiarAnomalia) {
    if (dataset->tamanho == 0) return;
    double soma = 0, min_val = dataset->dados[0].valor, max_val = dataset->dados[0].valor;
    int leiturasValidas = 0;
    for (int i = 0; i < dataset->tamanho; i++) {
        double val = dataset->dados[i].valor;
        if (val < limiarAnomalia) {
            soma += val;
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
            leiturasValidas++;
        }
    }
    if (leiturasValidas == 0) return;
    double media = soma / leiturasValidas;
    double variancia = 0;
    for (int i = 0; i < dataset->tamanho; i++)
        if (dataset->dados[i].valor < limiarAnomalia)
            variancia += pow(dataset->dados[i].valor - media, 2);
    variancia /= leiturasValidas;
    printf("\n--- [OP 1] ESTATISTICAS DOS SENSORES ---\n");
    printf("Limiar de anomalia (IQR Tukey): %.2f\n", limiarAnomalia);
    printf("Media: %.2f | Min: %.2f | Max: %.2f | Desvio Padrao: %.2f\n",
           media, min_val, max_val, sqrt(variancia));
}

void preverTendenciaSensor(VetorLeituras* dataset, int sensor_id, int janela, double limiarAnomalia) {
    double soma = 0; int cont = 0;
    for (int i = dataset->tamanho - 1; i >= 0 && cont < janela; i--) {
        if (dataset->dados[i].sensor_id == sensor_id && dataset->dados[i].valor < limiarAnomalia) {
            soma += dataset->dados[i].valor;
            cont++;
        }
    }
    if (cont > 0) {
        printf("--- [OP 2] PREVISAO DE TENDENCIA ---\n");
        printf("Sensor %d (ultimas %d leituras validas) -> Proxima leitura: %.2f\n",
               sensor_id, cont, soma / cont);
    }
}

int compararLeiturasDesc(const void* a, const void* b) {
    double da = ((LeituraSensor*)a)->valor, db = ((LeituraSensor*)b)->valor;
    if (da < db) return  1;
    if (da > db) return -1;
    return 0;
}

void filtrarEOrdenarAlertasCriticos(VetorLeituras* dataset, double limiteCritico, double limiarAnomalia) {
    VetorLeituras criticos;
    vetor_init(&criticos, 100);
    for (int i = 0; i < dataset->tamanho; i++) {
        double v = dataset->dados[i].valor;
        if (v >= limiteCritico && v < limiarAnomalia) // [FIX G] era 1000.0 hardcoded
            vetor_push(&criticos, dataset->dados[i]);
    }
    qsort(criticos.dados, criticos.tamanho, sizeof(LeituraSensor), compararLeiturasDesc);
    printf("\n--- [OP 3] TOP 5 ALERTAS CRITICOS (%.2f <= valor < %.2f) ---\n",
           limiteCritico, limiarAnomalia);
    for (int i = 0; i < 5 && i < criticos.tamanho; i++)
        printf("Sensor %d | Valor: %.2f | Timestamp: %lld\n",
               criticos.dados[i].sensor_id, criticos.dados[i].valor, criticos.dados[i].timestamp);
    vetor_free(&criticos);
}

// ==========================================
// 4. MÓDULOS DE SIMULAÇÃO
// ==========================================
VetorLeituras gerarDatasetRestrito(int quantidade) {
    VetorLeituras dataset;
    vetor_init(&dataset, quantidade);
    long long tempoAtual = 1700000000;
    int pacotesPerdidos = 0, anomaliasGeradas = 0;
    for (int i = 0; i < quantidade; ++i) {
        // [R16] 20% de perda de pacotes
        if (rand() % 100 < 20) { pacotesPerdidos++; tempoAtual += 10; continue; }
        LeituraSensor leitura;
        leitura.timestamp = tempoAtual + (i * 10) + (rand() % 500);
        leitura.sensor_id = (i % 5) + 1;
        // [R18] 10% de leituras anômalas
        if (rand() % 100 < 10) { leitura.valor = 9999.99; anomaliasGeradas++; }
        else leitura.valor = aplicarRuido(30.0 + ((rand() % 100) / 10.0), 0.0, 1.5);
        vetor_push(&dataset, leitura);
    }
    printf(">> SIMULACAO DE DADOS (R16 e R18) concluida.\n");
    printf("   - Pacotes perdidos na rede  : %d\n", pacotesPerdidos);
    printf("   - Anomalias de sensor (R18) : %d\n", anomaliasGeradas);
    return dataset;
}

void rodarSimulacaoComRestricoes(VetorLeituras* dataset, CuckooHash* ch) {
    TabelaHashSensores tabelaHash;
    Hash_init(&tabelaHash, 15013);
    ListaSensores listaIneficiente;
    Lista_init(&listaIneficiente);
    int LIMITE_RAM_HEAP = 1000;
    int descartesMemoria = 0;
    int cuckoFalhas = 0; // [FIX B] contador de falhas de inserção Cuckoo
    HeapSensores heapAlertas;
    Heap_init(&heapAlertas, LIMITE_RAM_HEAP);

    printf("\n=====================================================\n");
    printf("  INICIANDO INSERCAO COM RESTRICAO DE PROCESSAMENTO  \n");
    printf("=====================================================\n");

    double inicioIns = obter_tempo_ms();
    for (int i = 0; i < dataset->tamanho; ++i) {
        // [FIX B] Verifica retorno de Cuckoo_inserir e contabiliza falhas
        if (!Cuckoo_inserir(ch, dataset->dados[i])) cuckoFalhas++;
        Hash_inserir(&tabelaHash, dataset->dados[i]);
        Lista_inserir(&listaIneficiente, dataset->dados[i]);
        if (dataset->dados[i].valor > 100.0) {
            // [R5] Descarte por limite de RAM
            if (Heap_tamanho(&heapAlertas) >= LIMITE_RAM_HEAP) descartesMemoria++;
            else Heap_inserir(&heapAlertas, dataset->dados[i]);
        }
        if (i > 0 && i % 5000 == 0) dormir_ms(100); // [R10]
        if (rand() % 1000 == 0)     dormir_ms(500);  // [R12]
    }
    double fimIns = obter_tempo_ms();

    printf(" -> Tempo total de insercao (com interrupcoes/delays): %.2f ms\n", fimIns - inicioIns);
    printf(" -> Descartes por limite de RAM no Heap (R5)         : %d alertas perdidos\n", descartesMemoria);
    printf(" -> Falhas de insercao Cuckoo (fator de carga alto)  : %d\n", cuckoFalhas); // [FIX B]

    // [R21] Hash vs Busca Linear
    printf("\n=====================================================\n");
    printf("  [R21] RESTRICAO ALGORITMICA: HASH vs BUSCA LINEAR  \n");
    printf("=====================================================\n");
    long long alvosBusca[500];
    for(int i = 0; i < 500; i++) alvosBusca[i] = dataset->dados[rand() % dataset->tamanho].timestamp;
    LeituraSensor temp;
    double inicioHash  = obter_tempo_ms();
    for (int i = 0; i < 500; i++) Hash_buscar(&tabelaHash, alvosBusca[i], &temp);
    double fimHash = obter_tempo_ms();
    double inicioLista = obter_tempo_ms();
    for (int i = 0; i < 500; i++) Lista_buscar(&listaIneficiente, alvosBusca[i], &temp);
    double fimLista = obter_tempo_ms();
    printf(" -> 500 buscas na Tabela Hash O(1)     : %.4f ms\n", fimHash  - inicioHash);
    printf(" -> 500 buscas na Lista Encadeada O(n) : %.4f ms\n", fimLista - inicioLista);

    // [ITEM 7] Cuckoo vs Hash Original
    printf("\n=====================================================\n");
    printf("  [ITEM 7] ESTRUTURA OTIMIZADA vs ORIGINAL           \n");
    printf("  Cuckoo Hash (O(1) garantido) vs Hash Encadeada     \n");
    printf("=====================================================\n");
    long long alvosOt[1000];
    for (int i = 0; i < 1000; i++)
        alvosOt[i] = dataset->dados[rand() % dataset->tamanho].timestamp;
    LeituraSensor tmpOt;
    double tOrigem = obter_tempo_ms();
    for (int i = 0; i < 1000; i++) Hash_buscar(&tabelaHash, alvosOt[i], &tmpOt);
    double tempoBuscaOrig   = obter_tempo_ms() - tOrigem;
    double tCuckoo = obter_tempo_ms();
    for (int i = 0; i < 1000; i++) Cuckoo_buscar(ch, alvosOt[i], &tmpOt);
    double tempoBuscaCuckoo = obter_tempo_ms() - tCuckoo;

    printf(" Colisoes na Hash Original : %d\n", tabelaHash.totalColisoes);
    printf(" Colisoes no Cuckoo Hash   : 0 (por design)\n");
    printf(" 1000 buscas Hash Original : %.4f ms\n", tempoBuscaOrig);
    printf(" 1000 buscas Cuckoo Hash   : %.4f ms\n", tempoBuscaCuckoo);
    if (tempoBuscaCuckoo > 0)
        printf(" Ganho de velocidade       : %.2fx\n", tempoBuscaOrig / tempoBuscaCuckoo);

    // [FIX D] Cálculo de memória usando tabelaHash.capacidade (capacidade real alocada = 15013)
    //         Em vez de dataset->tamanho * 2, que não reflete o que foi de fato alocado
    printf(" Memoria Hash Original (cap=%d): ~%zu Bytes\n",
           tabelaHash.capacidade,
           (size_t)tabelaHash.capacidade * sizeof(NoLista*) +
           (size_t)tabelaHash.tamanho    * sizeof(NoLista));
    printf(" Memoria Cuckoo Hash (2x%d) : ~%zu Bytes (tabela fixa)\n",
           CAPACIDADE_CUCKOO,
           (size_t)CAPACIDADE_CUCKOO * sizeof(LeituraSensor) * 2 +
           (size_t)CAPACIDADE_CUCKOO * sizeof(bool)          * 2);

    Lista_free(&listaIneficiente);
    Hash_free(&tabelaHash);
    Heap_free(&heapAlertas);
}

// ==========================================
// VISUALIZAÇÃO — GRÁFICO DE BARRAS ASCII
// ==========================================
void imprimirGraficoBarras(const char* titulo, const char* nomes[], double valores[],
                            int quantidade, const char* unidade) {
    printf("\n--- %s ---\n", titulo);
    double maxVal = 0.0;
    for (int i = 0; i < quantidade; i++) if (valores[i] > maxVal) maxVal = valores[i];
    if (maxVal == 0.0) return;
    int largura = 40;
    for (int i = 0; i < quantidade; i++) {
        int barras = (int)((valores[i] / maxVal) * largura);
        printf("%-14s |", nomes[i]);
        for (int j = 0; j < barras; j++) printf("#");
        for (int j = barras; j < largura; j++) printf(" ");
        printf("| %.4f %s\n", valores[i], unidade);
    }
    printf("\n");
}

void analisarComplexidadeReal(const char* nome, const char* teorico,
                               double tempos[], int tamanhos[], int qtd) {
    if (qtd < 2) return;
    printf("\n--- Complexidade Real: %s (Teorico: %s) ---\n", nome, teorico);
    for (int i = 1; i < qtd; i++) {
        double razaoReal = tempos[i] / tempos[0];
        double razaoN    = (double)tamanhos[i] / (double)tamanhos[0];
        double razaoLog  = log((double)tamanhos[i]) / log((double)tamanhos[0]);
        printf("N=%d vs N=%d:\n", tamanhos[0], tamanhos[i]);
        printf("  Razao real medida      : %.3fx\n", razaoReal);
        printf("  Esperada O(1)          : 1.000x\n");
        printf("  Esperada O(log n)      : %.3fx\n", razaoLog);
        printf("  Esperada O(n)          : %.3fx\n", razaoN);
        double dC = fabs(razaoReal - 1.0), dL = fabs(razaoReal - razaoLog), dN = fabs(razaoReal - razaoN);
        const char* cls = (dC <= dL && dC <= dN) ? "O(1)" : (dL <= dN ? "O(log n)" : "O(n)");
        printf("  Classe mais proxima    : %s\n", cls);
    }
}

// ==========================================
// [FIX F] MENU INTERATIVO
// Adiciona consultas interativas ao sistema conforme exigido pelo enunciado.
// Opera sobre o dataset e uma hash table local para demonstrar buscas rápidas.
// ==========================================
void rodarMenuInterativo(VetorLeituras* dataset, double limiarAnomalia) {
    // Hash table local para buscas O(1) no menu
    TabelaHashSensores hashMenu;
    Hash_init(&hashMenu, dataset->tamanho * 2 + 1);
    for (int i = 0; i < dataset->tamanho; i++)
        Hash_inserir(&hashMenu, dataset->dados[i]);

    int opcao;
    do {
        printf("\n+------------------------------------+\n");
        printf("|    SISTEMA DE SENSORES — MENU     |\n");
        printf("+------------------------------------+\n");
        printf("| 1. Inserir nova leitura           |\n");
        printf("| 2. Buscar leitura por timestamp   |\n");
        printf("| 3. Remover leitura por timestamp  |\n");
        printf("| 4. Exibir estatisticas gerais     |\n");
        printf("| 5. Top 5 alertas criticos         |\n");
        printf("| 6. Prever tendencia de sensor     |\n");
        printf("| 0. Continuar para simulacao       |\n");
        printf("+------------------------------------+\n");
        printf("Opcao: ");
        if (scanf("%d", &opcao) != 1) { while (getchar() != '\n'); continue; }

        switch (opcao) {
            case 1: {
                LeituraSensor nova;
                printf("Sensor ID (1-5) : "); scanf("%d",  &nova.sensor_id);
                printf("Valor medido    : "); scanf("%lf", &nova.valor);
                nova.timestamp = (long long)time(NULL) + (rand() % 1000);
                vetor_push(dataset, nova);
                // Nota: vetor_push recebe VetorLeituras* e LeituraSensor
                Hash_inserir(&hashMenu, nova);
                printf("[OK] Inserido: Sensor %d | Valor %.2f | Timestamp %lld\n",
                       nova.sensor_id, nova.valor, nova.timestamp);
                break;
            }
            case 2: {
                long long ts; printf("Timestamp: "); scanf("%lld", &ts);
                LeituraSensor res;
                if (Hash_buscar(&hashMenu, ts, &res))
                    printf("[OK] Sensor %d | Valor: %.2f | Timestamp: %lld\n",
                           res.sensor_id, res.valor, res.timestamp);
                else
                    printf("[NF] Timestamp %lld nao encontrado.\n", ts);
                break;
            }
            case 3: {
                long long ts; printf("Timestamp a remover: "); scanf("%lld", &ts);
                if (Hash_remover(&hashMenu, ts))
                    printf("[OK] Timestamp %lld removido da Hash.\n", ts);
                else
                    printf("[NF] Timestamp %lld nao encontrado.\n", ts);
                break;
            }
            case 4:
                calcularEstatisticasGerais(dataset, limiarAnomalia);
                break;
            case 5:
                filtrarEOrdenarAlertasCriticos(dataset, 32.0, limiarAnomalia);
                break;
            case 6: {
                int sid, janela;
                printf("Sensor ID   : "); scanf("%d", &sid);
                printf("Janela (n)  : "); scanf("%d", &janela);
                preverTendenciaSensor(dataset, sid, janela, limiarAnomalia);
                break;
            }
            case 0: printf("Saindo do menu. Iniciando simulacao...\n"); break;
            default: printf("Opcao invalida.\n");
        }
    } while (opcao != 0);

    Hash_free(&hashMenu);
}

// ==========================================
// BENCHMARK GLOBAL — correções B, E, H, I
// ==========================================
void rodarBenchmarkGlobal(VetorLeituras* dataset, double limiarAnomalia) {
    // [FIX H] Faixa de N ampliada para ~10x de variação (antes: 10k/15k/20k = 2x)
    //         Isso torna a análise assintótica visualmente significativa
    int tamanhosTeste[] = {2000, 10000, dataset->tamanho};
    int numTestes = sizeof(tamanhosTeste) / sizeof(tamanhosTeste[0]);

    double historicoTHashOrig[10], historicoTHashOt[10], historicoTAvl[10],
           historicoTSkip[10],     historicoTST[10],      historicoTHeap[10]; // [FIX I]

    for (int t = 0; t < numTestes; t++) {
        int N = tamanhosTeste[t];

        printf("\n=====================================================\n");
        printf("  BENCHMARK ESCALABILIDADE: N = %d AMOSTRAS          \n", N);
        printf("=====================================================\n");

        // --- INICIALIZAÇÃO ---
        CuckooHash hashCuckoo;
        hashCuckoo.tabela1  = (LeituraSensor*)calloc(CAPACIDADE_CUCKOO, sizeof(LeituraSensor));
        hashCuckoo.tabela2  = (LeituraSensor*)calloc(CAPACIDADE_CUCKOO, sizeof(LeituraSensor));
        hashCuckoo.ocupado1 = (bool*)calloc(CAPACIDADE_CUCKOO, sizeof(bool));
        hashCuckoo.ocupado2 = (bool*)calloc(CAPACIDADE_CUCKOO, sizeof(bool));
        // [FIX J]
        if (!hashCuckoo.tabela1 || !hashCuckoo.tabela2 || !hashCuckoo.ocupado1 || !hashCuckoo.ocupado2) {
            fprintf(stderr, "FATAL: calloc falhou ao inicializar CuckooHash no benchmark\n"); exit(1);
        }

        TabelaHashSensores hashOriginal; Hash_init(&hashOriginal, N * 2);
        ArvoreAVLSensores  avl;          AVL_init(&avl);
        SkipListSensores   skip;         SkipList_init(&skip);
        HeapSensores       heapBenchmark; Heap_init(&heapBenchmark, N * 2);

        // [FIX H + FIX E] Subconjunto de N elementos para benchmark consistente da SegTree
        // No código original a SegTree era sempre construída sobre o dataset inteiro,
        // ignorando N — o que invalidava a análise de escalabilidade.
        VetorLeituras subconjunto;
        vetor_init(&subconjunto, N);
        for (int i = 0; i < N; i++) vetor_push(&subconjunto, dataset->dados[i]);
        SegmentTreeSensores st;

        // --- INSERÇÃO ---
        double tInicio;
        int falhasCuckoo = 0; // [FIX B]

        tInicio = obter_tempo_ms();
        for (int i = 0; i < N; i++)
            if (!Cuckoo_inserir(&hashCuckoo, dataset->dados[i])) falhasCuckoo++; // [FIX B]
        double tHashOtIns = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < N; i++) Hash_inserir(&hashOriginal, dataset->dados[i]);
        double tHashOrigIns = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < N; i++) AVL_inserir(&avl, dataset->dados[i]);
        double tAvlIns = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < N; i++) SkipList_inserir(&skip, dataset->dados[i]);
        double tSkipIns = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < N; i++) Heap_inserir(&heapBenchmark, dataset->dados[i]);
        double tHeapIns = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        SegmentTree_init(&st, &subconjunto); // [FIX H] constrói sobre N elementos
        double tSTIns = obter_tempo_ms() - tInicio;

        printf(" [INFO] Falhas de insercao Cuckoo (N=%d): %d (%.1f%%)\n",
               N, falhasCuckoo, (double)falhasCuckoo / N * 100); // [FIX B]

        // --- BUSCA ---
        long long alvosBusca[1000];
        for(int i = 0; i < 1000; i++) alvosBusca[i] = dataset->dados[rand() % N].timestamp;
        LeituraSensor temp;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) Cuckoo_buscar(&hashCuckoo, alvosBusca[i], &temp);
        double tHashOtBusca = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) Hash_buscar(&hashOriginal, alvosBusca[i], &temp);
        double tHashOrigBusca = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) AVL_buscar(&avl, alvosBusca[i], &temp);
        double tAvlBusca = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) SkipList_buscar(&skip, alvosBusca[i], &temp);
        double tSkipBusca = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) {
            int l = rand() % (N / 2);
            int r = l + (rand() % (N / 2));
            if (r >= N) r = N - 1;
            SegmentTree_consultarInterno(&st, 0, 0, st.n - 1, l, r);
        }
        double tSTBusca = obter_tempo_ms() - tInicio;

        historicoTHashOrig[t] = tHashOrigBusca;
        historicoTHashOt[t]   = tHashOtBusca;
        historicoTAvl[t]      = tAvlBusca;
        historicoTSkip[t]     = tSkipBusca;
        historicoTST[t]       = tSTBusca;

        // --- REMOÇÃO ---
        long long alvosRemocao[1000];
        for(int i = 0; i < 1000; i++) alvosRemocao[i] = dataset->dados[rand() % N].timestamp;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) Cuckoo_remover(&hashCuckoo, alvosRemocao[i]);
        double tHashOtRemoc = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) Hash_remover(&hashOriginal, alvosRemocao[i]);
        double tHashOrigRemoc = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) AVL_remover(&avl, alvosRemocao[i]);
        double tAvlRemoc = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) SkipList_remover(&skip, alvosRemocao[i]);
        double tSkipRemoc = obter_tempo_ms() - tInicio;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < 1000; i++) {
            LeituraSensor mx;
            Heap_extrairMaximo(&heapBenchmark, &mx);
        }
        double tHeapRemoc = obter_tempo_ms() - tInicio;

        historicoTHeap[t] = tHeapRemoc; // [FIX I] 1000 extrações escalam como O(log n)

        // [FIX E] Demonstração real de SegmentTree_remover:
        // Remove logicamente todas as leituras anômalas (>= limiarAnomalia) do índice,
        // provando que a função tem propósito útil: após a remoção, consultas de
        // máximo em qualquer intervalo retornam apenas leituras válidas.
        double maxAntes = SegmentTree_consultarInterno(&st, 0, 0, st.n - 1, 0, st.n - 1);
        int anomaliasRemovidasST = 0;
        for (int i = 0; i < subconjunto.tamanho; i++) {
            if (subconjunto.dados[i].valor >= limiarAnomalia) {
                SegmentTree_remover(&st, i);
                anomaliasRemovidasST++;
            }
        }
        double maxDepois = SegmentTree_consultarInterno(&st, 0, 0, st.n - 1, 0, st.n - 1);
        printf("\n>>> [FIX E] SegmentTree_remover — limpeza de anomalias:\n");
        printf(" Max ANTES da limpeza  : %.2f (inclui anomalias)\n", maxAntes);
        printf(" Posicoes removidas    : %d (valor >= %.2f)\n", anomaliasRemovidasST, limiarAnomalia);
        printf(" Max DEPOIS da limpeza : %.2f (somente leituras validas)\n", maxDepois);

        // --- LATÊNCIA CICLO COMBINADO ---
        int CICLOS = 200;
        double tHashOrigCiclo = 0, tHashOtCiclo = 0, tAvlCiclo = 0, tSkipCiclo = 0;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < CICLOS; i++) {
            LeituraSensor nova = dataset->dados[rand() % N]; nova.timestamp += 999999999LL;
            Hash_inserir(&hashOriginal, nova); LeituraSensor tmp;
            Hash_buscar(&hashOriginal, nova.timestamp, &tmp);
            Hash_remover(&hashOriginal, nova.timestamp);
        }
        tHashOrigCiclo = (obter_tempo_ms() - tInicio) / CICLOS;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < CICLOS; i++) {
            LeituraSensor nova = dataset->dados[rand() % N]; nova.timestamp += 999999999LL;
            Cuckoo_inserir(&hashCuckoo, nova); LeituraSensor tmp;
            Cuckoo_buscar(&hashCuckoo, nova.timestamp, &tmp);
            Cuckoo_remover(&hashCuckoo, nova.timestamp);
        }
        tHashOtCiclo = (obter_tempo_ms() - tInicio) / CICLOS;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < CICLOS; i++) {
            LeituraSensor nova = dataset->dados[rand() % N]; nova.timestamp += 999999999LL;
            AVL_inserir(&avl, nova); LeituraSensor tmp;
            AVL_buscar(&avl, nova.timestamp, &tmp);
            AVL_remover(&avl, nova.timestamp);
        }
        tAvlCiclo = (obter_tempo_ms() - tInicio) / CICLOS;

        tInicio = obter_tempo_ms();
        for (int i = 0; i < CICLOS; i++) {
            LeituraSensor nova = dataset->dados[rand() % N]; nova.timestamp += 999999999LL;
            SkipList_inserir(&skip, nova); LeituraSensor tmp;
            SkipList_buscar(&skip, nova.timestamp, &tmp);
            SkipList_remover(&skip, nova.timestamp);
        }
        tSkipCiclo = (obter_tempo_ms() - tInicio) / CICLOS;

        // --- IMPRESSÃO DOS RESULTADOS ---
        printf("\n>>> Tempo Medio de Consulta (1000 buscas)\n");
        printf("Hash Orig : %.6f ms/op\n", tHashOrigBusca / 1000.0);
        printf("Cuckoo    : %.6f ms/op\n", tHashOtBusca   / 1000.0);
        printf("AVL Tree  : %.6f ms/op\n", tAvlBusca      / 1000.0);
        printf("Skip List : %.6f ms/op\n", tSkipBusca     / 1000.0);
        printf("Seg. Tree : %.6f ms/op (consulta de intervalo)\n", tSTBusca / 1000.0);
        printf("Max Heap  : N/A (sem busca arbitraria por design)\n");

        printf("\n>>> Latencia Media por Ciclo Combinado (Ins+Busca+Rem)\n");
        printf("Hash Orig: %.4f ms | Cuckoo: %.4f ms | AVL: %.4f ms | Skip: %.4f ms\n",
               tHashOrigCiclo, tHashOtCiclo, tAvlCiclo, tSkipCiclo);

        printf("\n>>> GRUPO A: Busca por chave (comparaveis)\n");
        printf("METRICA      | Hash Orig  | Cuckoo Hash| AVL Tree   | Skip List\n");
        printf("-------------|------------|------------|------------|----------\n");
        printf("Ins.         | %7.2f ms | %7.2f ms | %7.2f ms | %7.2f ms\n",
               tHashOrigIns, tHashOtIns, tAvlIns, tSkipIns);
        printf("Busca        | %7.2f ms | %7.2f ms | %7.2f ms | %7.2f ms\n",
               tHashOrigBusca, tHashOtBusca, tAvlBusca, tSkipBusca);
        printf("Remoc.       | %7.2f ms | %7.2f ms | %7.2f ms | %7.2f ms\n",
               tHashOrigRemoc, tHashOtRemoc, tAvlRemoc, tSkipRemoc);
        printf("Colisoes     | %7d    |      N/A   |      N/A   |      N/A\n",
               hashOriginal.totalColisoes);

        printf("\n>>> GRUPO B: Finalidade distinta (nao comparaveis ao Grupo A)\n");
        printf("Max Heap     -> Ins: %.2f ms | Extr. Max: %.2f ms | (sem busca arbitraria)\n",
               tHeapIns, tHeapRemoc);
        printf("Segment Tree -> Const.: %.2f ms | Consulta intervalo: %.2f ms | (imutavel por posicao)\n",
               tSTIns, tSTBusca);

        long long memHashOrig = (long long)hashOriginal.capacidade * sizeof(NoLista*)
                              + (long long)hashOriginal.tamanho    * sizeof(NoLista);
        long long memCuckoo   = (long long)CAPACIDADE_CUCKOO * sizeof(LeituraSensor) * 2
                              + (long long)CAPACIDADE_CUCKOO * sizeof(bool)          * 2;
        long long memAvl      = (long long)avl.tamanho * sizeof(NoAVL);

        printf("\n>>> Uso de Memoria (RAM)\n");
        printf("- Hash Original (cap=%d): %lld Bytes\n", hashOriginal.capacidade, memHashOrig);
        printf("- Cuckoo Hash (2x%d)   : %lld Bytes\n", CAPACIDADE_CUCKOO, memCuckoo);
        printf("- Arvore AVL (%d nos)  : %lld Bytes\n", avl.tamanho, memAvl);

        const char* nomes[] = {"Hash Orig","Cuckoo Hash","AVL Tree","Skip List","Max Heap","Seg. Tree"};
        double valIns[] = {tHashOrigIns, tHashOtIns, tAvlIns, tSkipIns, tHeapIns,  tSTIns};
        double valBus[] = {tHashOrigBusca, tHashOtBusca, tAvlBusca, tSkipBusca, 0.0, tSTBusca};
        double valRem[] = {tHashOrigRemoc, tHashOtRemoc, tAvlRemoc, tSkipRemoc, tHeapRemoc, 0.0};
        imprimirGraficoBarras("GRAFICO: Insercao",  nomes, valIns, 6, "ms");
        imprimirGraficoBarras("GRAFICO: Busca",     nomes, valBus, 6, "ms");
        imprimirGraficoBarras("GRAFICO: Remocao",   nomes, valRem, 6, "ms");

        // --- LIBERAÇÃO ---
        vetor_free(&subconjunto);
        Cuckoo_free(&hashCuckoo);
        Hash_free(&hashOriginal);
        AVL_free(&avl);
        SkipList_free(&skip);
        Heap_free(&heapBenchmark);
        SegmentTree_free(&st);
    }

    // --- ANÁLISE ASSINTÓTICA ---
    printf("\n=====================================================\n");
    printf("  ANALISE DE COMPLEXIDADE ASSINTOTICA REAL            \n");
    printf("  (N varia de %d a %d — %.1fx de amplitude)\n",
           tamanhosTeste[0], tamanhosTeste[numTestes-1],
           (double)tamanhosTeste[numTestes-1] / tamanhosTeste[0]);
    printf("=====================================================\n");

    printf("\n--- Grupo A: busca por chave ---\n");
    analisarComplexidadeReal("Hash Original (Encadeada)", "O(1) medio",       historicoTHashOrig, tamanhosTeste, numTestes);
    analisarComplexidadeReal("Cuckoo Hash",               "O(1) garantido",   historicoTHashOt,   tamanhosTeste, numTestes);
    analisarComplexidadeReal("Arvore AVL",                "O(log n)",         historicoTAvl,      tamanhosTeste, numTestes);
    analisarComplexidadeReal("Skip List",                 "O(log n) esperado",historicoTSkip,     tamanhosTeste, numTestes);

    printf("\n--- Grupo B: finalidade distinta ---\n");
    // [FIX I] Max Heap agora incluído na análise assintótica (1000 extrações ~ O(1000 log N))
    analisarComplexidadeReal("Max Heap (1000 extracoes do max)", "O(log n)",   historicoTHeap,    tamanhosTeste, numTestes);
    analisarComplexidadeReal("Segment Tree (consulta intervalo)", "O(log n)",  historicoTST,      tamanhosTeste, numTestes);
}

// ==========================================
// 5. FUNÇÃO PRINCIPAL
// ==========================================
int main() {
    srand(42);
    int qtdAmostras = 25000;

    printf("Gerando dataset sintetico com anomalias e falhas de rede...\n");
    VetorLeituras dadosReais = gerarDatasetRestrito(qtdAmostras);

    // [FIX G] Calcula limiar estatístico robusto — substitui 1000.0 hardcoded em todo o código
    double limiarAnomalia = calcularLimiarAnomalia(&dadosReais);
    printf(">> Limiar de anomalia calculado (Q3 + 3*IQR): %.2f\n\n", limiarAnomalia);

    // [FIX F] Menu interativo — exigido pelo enunciado ("consultas interativas")
    rodarMenuInterativo(&dadosReais, limiarAnomalia);

    // Operações adicionais (OP 1, 2, 3)
    calcularEstatisticasGerais(&dadosReais, limiarAnomalia);
    preverTendenciaSensor(&dadosReais, 3, 15, limiarAnomalia);
    filtrarEOrdenarAlertasCriticos(&dadosReais, 32.0, limiarAnomalia);

    // Cuckoo Hash global (para simulação com restrições)
    CuckooHash tabelaCuckoo;
    tabelaCuckoo.tabela1  = (LeituraSensor*)calloc(CAPACIDADE_CUCKOO, sizeof(LeituraSensor));
    tabelaCuckoo.tabela2  = (LeituraSensor*)calloc(CAPACIDADE_CUCKOO, sizeof(LeituraSensor));
    tabelaCuckoo.ocupado1 = (bool*)calloc(CAPACIDADE_CUCKOO, sizeof(bool));
    tabelaCuckoo.ocupado2 = (bool*)calloc(CAPACIDADE_CUCKOO, sizeof(bool));
    // [FIX J]
    if (!tabelaCuckoo.tabela1 || !tabelaCuckoo.tabela2 ||
        !tabelaCuckoo.ocupado1 || !tabelaCuckoo.ocupado2) {
        fprintf(stderr, "FATAL: calloc falhou ao inicializar tabelaCuckoo em main\n"); exit(1);
    }

    rodarSimulacaoComRestricoes(&dadosReais, &tabelaCuckoo);
    rodarBenchmarkGlobal(&dadosReais, limiarAnomalia);

    Cuckoo_free(&tabelaCuckoo);
    vetor_free(&dadosReais);
    return 0;
}
