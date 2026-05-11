export module zero.driver.handler;

import zero.driver.pipeline;

export auto run(const Driver& driver) -> int;
export auto dump(const Driver& driver) -> int;
export auto build(const Driver& driver) -> int;
export auto check(const Driver& driver) -> int;
