import pytest

from mutools.adapters.web.library import PAGE_SIZE, page_count, page_numbers


@pytest.mark.parametrize(("files", "pages"), [(0, 1), (PAGE_SIZE, 1), (PAGE_SIZE + 1, 2), (248, 31)])
def test_the_plates_fill_pages_of_a_fixed_size(files: int, pages: int) -> None:
    assert page_count(files) == pages


@pytest.mark.parametrize(
    ("page", "pages", "offered"),
    [
        (1, 1, [1]),
        (4, 7, [1, 2, 3, 4, 5, 6, 7]),
        (1, 15, [1, 2, 3, 4, 5, None, 15]),
        (4, 15, [1, 2, 3, 4, 5, None, 15]),
        (5, 15, [1, None, 4, 5, 6, None, 15]),
        (11, 15, [1, None, 10, 11, 12, None, 15]),
        (12, 15, [1, None, 11, 12, 13, 14, 15]),
        (15, 15, [1, None, 11, 12, 13, 14, 15]),
    ],
)
def test_the_pager_offers_both_ends_and_the_pages_around(page: int, pages: int, offered: list[int | None]) -> None:
    assert page_numbers(page, pages) == offered
