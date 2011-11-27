LogData = importdata('logfile.dat');
Time = LogData.data(:,1);
Window = LogData.data(:,2);
Byterate = LogData.data(:,3);
Throughput = LogData.data(:,4);

figure('Name', 'Window size', 'NumberTitle', 'off');
plot(Time, Window);
xlabel('Time (s)');
ylabel('Window size (bytes)');

figure('Name', 'Byte rate', 'NumberTitle', 'off');
plot(Time, Byterate);
xlabel('Time (s)');
ylabel('Byte rate (raw output)');

figure('Name', 'Throughput', 'NumberTitle', 'off');
plot(Time, Throughput);
xlabel('Time (s)');
ylabel('Throughput (data output)');
