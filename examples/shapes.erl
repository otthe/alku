-module(shapes).
-export([area/1, total_area/1, describe/1]).

area({circle, R}) -> math:pi() * R * R;
area({square, Side}) -> Side * Side;
area({rectangle, W, H}) -> W * H.

total_area(Shapes) -> geometry_util:sum([area(S) || S <- Shapes]).

describe(Shape) ->
    "a " ++ atom_to_list(element(1, Shape)) ++ " with area " ++
        integer_to_list(round(area(Shape))).
