# Origem e instalação local

Pacote: [ezrover/ESP32-AI-Agent-Skill](https://github.com/ezrover/ESP32-AI-Agent-Skill).
Commit: `124d298e4ccaa737dbf6d26bbae47722e17f3097`.
Licença MIT: copyright (c) 2026 Vibe Coder; texto integral em `LICENSE`.

`SKILL.md` foi instalado pelo helper oficial `skill-installer`, com `--dest`
apontando somente para `.agents/skills` deste projeto. Os arquivos upstream
`README.md`, `LICENSE`, `references/` e `scripts/` foram copiados sem alterações
para esta pasta para preservar o pacote de suporte. Nenhum instalador Claude,
hook ou configuração global foi usado.

Neste empacotamento, a raiz do plugin mencionada pelo `SKILL.md` corresponde
a esta pasta: `.agents/skills/esp32`. O checkout original fica em
`.tools/esp32-ai-agent-skill-124d298e`, local e ignorado pelo Git.

O inventário de hashes está em `tools/esp32-audit-upstream.sha256` na raiz
do projeto. `SKILL.md`, scripts e referências mantêm os bytes do commit fixado.
Este arquivo é uma nota de integração do NGK ECU.

Ler `docs/ferramentas-esp32.md` na raiz do projeto antes de aplicar sugestões.
São referências auxiliares; as regras do projeto, o perfil já validado em bancada
e a documentação oficial dos componentes continuam prevalecendo. A skill não
intercepta comandos nem impede ações de um agente automaticamente.
