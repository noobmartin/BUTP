GoodputLogData = importdata('server_goodput.dat');
GoodTime = GoodputLogData(:,1);
GoodByterate = GoodputLogData(:,2);

figure('Name', 'Goodput', 'NumberTitle', 'off');
plot(GoodTime, GoodByterate);
xlabel('Time (s)');
ylabel('Goodput (Bps)');
