# Licenciamento deste acervo

Este repositório reúne **trabalho próprio** e **material de terceiros**. Cada
parte tem a sua licença, descrita abaixo. Em caso de dúvida sobre um arquivo
específico, vale a seção correspondente e a licença declarada no próprio
original.

## 1. Trabalho próprio — CC BY-SA 4.0

Aplica-se a:

- `docs/` — relatórios de análise, evidências extraídas e textos produzidos aqui;
- `firmware/` — o firmware próprio para o STM32L072CZT6 (código, build e testes);
- `tools/` — o analisador de firmware e o decodificador de payload;
- `README.md`, este `LICENSE.md` e as demais descrições do projeto.

Você pode copiar, adaptar e redistribuir esse material, inclusive para fins
comerciais, desde que:

1. dê o **crédito** apropriado (ver §3);
2. distribua as **adaptações sob a mesma licença** (*ShareAlike*).

- Resumo: <https://creativecommons.org/licenses/by-sa/4.0/deed.pt-br>
- Texto legal: <https://creativecommons.org/licenses/by-sa/4.0/legalcode.pt>

Esta nota não substitui o texto legal.

## 2. Material de terceiros — permanece com a licença de origem

O material abaixo **não** é licenciado por este acervo; ele é redistribuído
como referência de estudo e continua pertencendo aos seus titulares.

| Material | Origem | Situação |
|---|---|---|
| `Node-Dragino - LSN50v2/LoRa ST Sensor Node v2.0.sch.pdf` e as renderizações derivadas (`docs/firmware/LSN50v2_esquematico_v2.0.png`, `docs/firmware/LSN50v2_bloco_titulo_CC-BY-SA-3.0.png`) | Dragino | **CC BY-SA 3.0**, declarada no bloco de título do próprio desenho: *"Released under the CC Attribution Share-Alike 3.0 License"* — <http://creativecommons.org/licenses/by-sa/3.0/> |
| `Node-Dragino - LSN50v2/LSN50_LoRa_Sensor_Node_UserManual_v1.7.4.pdf`, `DRAGINO_LSN50_AT_Commands_v1.6.3.pdf` e os `.md` convertidos ao lado | Dragino | Documentação do fabricante redistribuída como referência, sem licença declarada no próprio arquivo; © Dragino. Os `.md` são conversões fiéis desses PDFs e seguem a mesma situação |
| `docs/firmware/LSN50_fluxo_manual_2.1_p12.png` | Dragino (recorte do manual, §2.1) | idem acima |
| `FIRMWARE/LSN50_AU915_v1.8.1.hex` | Dragino | Imagem de firmware distribuída do produto, redistribuída para estudo; © Dragino. O **código-fonte** correspondente, publicado pela Dragino em <https://github.com/dragino/LoRa_STM32>, tem licenciamento próprio — a confirmar na origem |
| `datasheets/STM32L072CZT6-C1337587.pdf` | STMicroelectronics / distribuidor | Datasheet de componente redistribuído como referência; © STMicroelectronics |

**Atenção ao *ShareAlike*:** um derivado direto do material CC BY-SA 3.0 (por
exemplo, um recorte do esquemático) continua sob **CC BY-SA 3.0** e exige
atribuição e *ShareAlike* na redistribuição. Os textos de análise deste acervo
não derivam dessas figuras — apenas as citam e apontam para elas — e por isso
estão sob a licença da §1.

## 3. Como atribuir

Trabalho próprio deste acervo:

> Acervo de engenharia reversa do Dragino LSN50 — *título do arquivo*,
> CC BY-SA 4.0.

Material da Dragino (esquemático, manuais e imagem de firmware):

> DRAGINO. *LoRa ST Sensor Node v2.0 — esquemático*, www.dragino.com,
> CC BY-SA 3.0.
>
> DRAGINO. *LSN50 LoRaWAN Sensor Node User Manual v1.7.4* e
> *Dragino LoRa AT Command Sets v1.6.3*, www.dragino.com.

Ferramentas de terceiros usadas na análise (Capstone, IntelHex, PlatformIO,
GNU Arm Embedded Toolchain, Poppler, pandoc e demais) mantêm suas próprias
licenças e não são redistribuídas aqui.

## 4. Marcas e ausência de vínculo

"Dragino", "LSN50" e "LoRa ST" são marcas de seus titulares; "STM32" é marca da
STMicroelectronics. Este é um trabalho independente de estudo e engenharia
reversa: **não é afiliado, patrocinado nem endossado** pela Dragino ou pela
STMicroelectronics. Nada aqui concede direitos sobre marcas, logotipos ou sobre
o código-fonte original do fabricante.

## 5. Limites

O material é fornecido "como está", sem garantias de qualquer natureza. A
análise descreve um artefato entregue e registra explicitamente, no
[relatório](docs/firmware/LSN50_AU915_v1.8.1-analise.md), o que é observação,
o que é inferência e o que permanece desconhecido. Nada neste arquivo constitui
aconselhamento jurídico.
