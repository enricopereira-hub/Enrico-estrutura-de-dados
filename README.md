Este repositório contém o projeto final da disciplina de Estruturas de Dados do curso de Engenharia de Controle e Automação (UNESP Sorocaba). 

Autor: Enrico Rodrigues Pereira

Sobre o Projeto
O sistema simula um ambiente de telemetria industrial, processando até 25.000 amostras sintéticas de sensores de temperatura. O objetivo principal é a aplicação empírica e a comparação de desempenho (benchmark) de sete estruturas de dados diferentes sob restrições críticas de memória, processamento e latência.

Estrutura do Repositório
* `/src`: Código-fonte (`main.c`) totalmente em C, documentado e estruturado.
* `/bin`: Executável compilado e pronto para uso.
* `/docs`: Relatório técnico final (`Relatorio_ED2026.pdf`) contendo a fundamentação teórica, análise de complexidade assintótica real e benchmark gráfico.

 Estruturas de Dados Implementadas
 Grupo A (Busca Exata por Chave):
* Tabela Hash com Encadeamento Externo
* Cuckoo Hashing (Estrutura Otimizada)
* Árvore AVL
* Skip List Probabilística

Grupo B (Prioridade e Agregação):
* Max Heap (Gerenciamento de alertas)
* Segment Tree (Range Maximum Queries para dados históricos)

Como Executar
1. Navegue até a pasta `/bin`.
2. Execute o arquivo `simulador.exe` (Windows) ou `./simulador` (Linux/Mac).
3. Siga as instruções do Menu Interativo no terminal para consultas O(1) ou inicie a simulação completa de benchmark.

---

Em conformidade com a rubrica do projeto, o uso de IA generativa foi restrito ao suporte na codificação e depuração, não sendo utilizado na redação do relatório técnico. Abaixo estão os links para todas as conversas que geraram ou auxiliaram nos trechos de código desta versão final:

* **Conversa 1 (Gemini):** https://share.gemini.google/RilgV7MwlFKD
* **Conversa 2 (Gemini):** https://share.gemini.google/jOJ2kih1JkRQ
* **Conversa 3 (Gemini):** https://share.gemini.google/UptcgOH5FPvj
* **Conversa 4 (Gemini):** https://share.gemini.google/8jOhiz8GsQbT
* **Conversa 5 (Gemini):** https://share.gemini.google/1ZuAGQrvPuV9
* **Conversa 6 (Claude):** https://claude.ai/share/9670e632-b3c3-470a-ac89-c4b6dcefa899
