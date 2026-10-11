# Project 2

- Name: Anthony Baird
- Email: anthonybaird@u.boisestate.edu
- Class: CS425-001

## Known Bugs or Issues

None.

## Experience

I mostly used AI prompting to program this, and I was battling the AI a lot on this one.  In Codespaces, where I wrote the project, apparently the default Copilot setting is to allow almost all code changes without confirmation. So I was just going with that at first, which lead to a lot of unintended changes. Some specific scenarios I ran into were:
 - I was going through the assignment without reading to the end first. At first, I gave it the 3-layer instructions from p1. After most of the code was done, I saw the slightly different definitions for the 3-layer split in p2, and told it to double-check that we were fitting these new requirements. Claude immediately decided to delete all source files and start from scratch.
 - At several points, I told it to change the code in a very specific way. There was absolutely no ambiguity in my instructions, but it decided to ignore me and do whatever it thought was best instead, sometimes including things that weren't even prompted for at all - completely aside from the point. 
 - I had Copilot set to auto-select model, in balanced mode, and it usually chose to use Claude. Claude is very token-cost-heavy. Sometimes it does a lot for the amount of credits it costs, but several times I asked sub-10 word follow up questions about the way it wrote something, and the response costed 50+ tokens. I asked for the banner comments labeling the layer-level of the functions, because I was being lazy - that costed 60 tokens. 
 
 After a few of the doing-whatever-it-dang-well-wanted moments where Claude was straight-up ignoring me and doing other things instead of what I asked, I went and found the user-level setting in vscode's settings json to force user confirmation on every single agent action. I also switched from 'balanced' autoselect to 'efficiency', and started a new chat context which lead to MUCH lower token cost mostly using GPT Luna instead of Claude. All in all I spent roughly 800 tokens on this project (most of that was before those changes). Every question that is handled by Claude takes at least 20 tokens, and sometimes MUCH more. The deletion of the entire src directory and re-writing took ~100, and I had pretty much no control over that. Luckily, I still have the Copilot Pro subscription that I bought until the 20th, which gives me 1500 tokens/mo instead of the 200/mo student version. To be honest I will probably re-subscribe to it, because 200 tokens is just not nearly enough and getting the 1500 instead only costs $10. This class isn't the only place I need it; I use it a lot for my research project. I will probably just set the model straight to GPT Luna in the future, because Claude's token cost through Copilot is through-the-roof insane, and Luna typically costs less than one credit per prompt.

## Results

| Window | Loss Rate | Mean Time | Throughput (KiB/s) |
| :--- | :--- | :--- | :--- |
| 1 | 0 | 103.24 s | 9.92 |
| 1 | 0.05 | 132.19 s | 7.75 |
| 16 | 0 | 6.63 s | 154.47 |
| 16 | 0.05 | 23.56 s | 43.46 |

### Analysis

* **Round Trip Time (RTT):** With window size 1 and no loss,  mean time of 103.24 seconds by 1025 packets yielded an RTT of approximately 100.7 ms per packet. Removing the 100ms added by the relay, the remaining fraction (~0.7 ms) should be the total local processing delay at both the sender and receiver.
* **Window 16 Speedup:** The drop to 6.63s with window size 16 gave a speedup factor of about 15.6x. I think this is just short of 16x because 1025 % 16 = 1, meaning there is one packet left over at the end to send (in addition to a little natural variance).
* **Impact of 5% Loss:** When the window size 1 run has to resend, it only has to resend one packet. When the window size 16 run as to resend, it can have up to 16 that  have to be re-sent.
