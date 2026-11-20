"""Los controles de reference_summary.py: qué es cerrado contra el ASM, las cuentas por ola, la diferencia contra la
referencia commiteada y las secciones del resumen, con tablas chicas inventadas.

    python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_reference_summary.py' -v
"""
import csv
import pathlib
import tempfile
import unittest

import reference_summary as rs

FUNCIONES = ['va', 'bytes', 'clase', 'viva', 'ola', 'estado', 'categoria_faltante', 'falso_positivo', 'cortes_vivos',
             'cierra_el_cliente',
             'citas_pendiente', 'citas_test', 'pasos_vm', 'ids_08_abiertas']
METRICAS = ['ref', 'ambito', 'clave', 'metrica', 'valor', 'unidad', 'fuente', 'nota']
CORTES = ['archivo', 'línea', 'función', 'clase', 'cierra_el_cliente']
HUECOS = ['hueco', 'desde', 'hasta', 'funciones', 'bytes', 'ola', 'senal']


def closed(**changes):
    row = dict(va='0x401000', bytes='1024', clase='game', viva='sí', ola='CL8', estado='implemented',
               falso_positivo='', cortes_vivos='', cierra_el_cliente='', citas_pendiente='0', citas_test='2',
               pasos_vm='', ids_08_abiertas='')
    row.update(changes)
    return row


def metric(ambito, clave, metrica, valor):
    return dict(ref='abc', ambito=ambito, clave=clave, metrica=metrica, valor=str(valor), unidad='', fuente='', nota='')


def write(path, columns, rows):
    with open(path, 'w', encoding='utf-8', newline='') as fh:
        writer = csv.DictWriter(fh, fieldnames=columns, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row.get(k, '') for k in columns})


class ReferenceSummaryTest(unittest.TestCase):
    def test_field_size_limit_fits_a_32_bit_c_long(self):
        # En Windows el long de C es de 32 bits: con sys.maxsize, field_size_limit tira OverflowError al importar.
        self.assertLessEqual(csv.field_size_limit(), 2**31 - 1)

    def test_closed_needs_every_condition(self):
        self.assertTrue(rs.closed_against_asm(closed()))
        for change in (dict(estado='partial'), dict(falso_positivo='sí'), dict(cortes_vivos='G1'),
                       dict(citas_pendiente='1'), dict(citas_test='0'), dict(pasos_vm='lista-vm#1'),
                       dict(ids_08_abiertas='P-C70')):
            with self.subTest(change=change):
                self.assertFalse(rs.closed_against_asm(closed(**change)))
        dead = rs.closure_by_wave([closed(viva='no'), closed(clase='library', viva='')])
        self.assertEqual(dead['total']['funciones'], 0)

    def test_closure_by_wave_counts_kb_and_blockers(self):
        rows = [closed(bytes='1024'),
                closed(va='0x402000', bytes='2048', estado='unknown', citas_test='0'),
                closed(va='0x403000', bytes='512', estado='partial', cortes_vivos='P1',
                       cierra_el_cliente='Scenes/A.cpp:10'),
                closed(va='0x404000', bytes='100', ola='CL2')]
        waves = rs.closure_by_wave(rows)
        cl8 = waves['CL8']
        self.assertEqual(cl8['funciones'], 3)
        self.assertAlmostEqual(cl8['kb'], 3.5)
        self.assertEqual(cl8['cerradas'], 1)
        self.assertAlmostEqual(cl8['kb_cerrados'], 1.0)
        self.assertEqual(cl8['sin_citar'], 1)
        self.assertEqual(cl8['con_cortes'], 1)
        self.assertEqual(cl8['cierran'], 1)
        self.assertEqual(waves['total']['funciones'], 4)
        self.assertEqual(waves['total']['cerradas'], 2)
        self.assertAlmostEqual(waves['total']['kb'], 3.5 + 100 / 1024)

    def test_explained_functions_are_not_left_to_close(self):
        # Sin citar pero explicadas (un clon, la plataforma...): no frenan ni cuentan en lo que queda por cerrar.
        rows = [closed(bytes='1024'),
                closed(va='0x402000', bytes='2048', estado='unknown', citas_test='0',
                       categoria_faltante='clon byte a byte de una función citada'),
                closed(va='0x403000', bytes='512', estado='unknown', citas_test='0',
                       categoria_faltante='sin clasificar')]
        cl8 = rs.closure_by_wave(rows)['CL8']
        self.assertEqual(cl8['explicadas'], 1)
        self.assertAlmostEqual(cl8['kb_explicados'], 2.0)
        self.assertEqual(cl8['sin_citar'], 1)
        self.assertEqual(cl8['por_cerrar'], 2)
        self.assertAlmostEqual(cl8['kb_por_cerrar'], 1.5)

    def test_deltas_show_new_and_missing_metrics(self):
        old = [metric('cortes', 'total', 'sitios', 1754), metric('huecos', 'total', 'bloques', 299)]
        new = [metric('cortes', 'total', 'sitios', 1753), metric('huecos', 'total', 'bloques', 299),
               metric('07', 'otras', 'registrada', 75)]
        self.assertEqual(rs.metric_deltas(new, old),
                         [('cortes', 'total', 'sitios', '1754', '1753'), ('07', 'otras', 'registrada', None, '75')])

    def test_render_sections(self):
        with tempfile.TemporaryDirectory() as root:
            new = pathlib.Path(root, 'nueva')
            old = pathlib.Path(root, 'commiteada')
            for folder in (new, old):
                folder.mkdir()
                write(folder / 'funciones.tsv', FUNCIONES, [closed()])
                write(folder / 'metricas.tsv', METRICAS,
                      [metric('avance', 'total', 'cota_baja', 87.4), metric('avance-ola', 'CL1', 'cota_baja', 90)])
                write(folder / 'cortes.tsv', CORTES, [])
                write(folder / 'huecos.tsv', HUECOS, [])
            text = rs.render(new, old)
        self.assertIn('## Cuánto falta para portar', text)
        self.assertIn('## Cuánto falta para cerrarlo contra el ASM', text)
        self.assertIn('## El estado de las trazas', text)
        self.assertIn('Ninguno', text)
        self.assertIn('al día', text)


if __name__ == '__main__':
    unittest.main()
