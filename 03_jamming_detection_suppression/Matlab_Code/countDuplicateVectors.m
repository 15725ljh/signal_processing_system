function uniqueVectors = countDuplicateVectors(matrix, dim)
% 统计矩阵中相同向量的数量
% 输入：
%   matrix - 输入矩阵
%   dim    - 统计维度：1（列向量）或2（行向量），默认为行向量
% 输出：
%   uniqueVectors - 唯一向量
%   counts        - 每个唯一向量出现的次数

% 设置默认维度（行向量）
if nargin < 2
    dim = 2;
end

% 确保矩阵为2维
if ndims(matrix) > 2
    error('只支持2维矩阵');
end

% 根据维度处理矩阵
if dim == 1 % 列向量
    matrix = matrix';
end

% 获取唯一行向量及其索引
[uniqueVectors, ~, idx] = unique(matrix, 'rows', 'stable');

% 统计每个唯一向量的出现次数
counts = accumarray(idx, 1);

% 按出现次数降序排序
[counts, sortIdx] = sort(counts, 'descend');
uniqueVectors = uniqueVectors(sortIdx, :);

% 显示结果（简洁模式）
for i = 1:size(uniqueVectors, 1)
    val = uniqueVectors(i, 1);
    fprintf('  类型%d: %d/%d (%.1f%%)', val, counts(i), sum(counts), 100*counts(i)/sum(counts));
    if i < size(uniqueVectors, 1), fprintf('  |'); end
end
fprintf('\n');
end